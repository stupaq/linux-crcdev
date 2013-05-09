#include <linux/pci.h>
#include "crcdev.h"
#include "pci.h"
#include "errors.h"
#include "concepts.h"
#include "interrupts.h"
#include "chrdev.h"
#include "sysfs.h"
#include "monitors.h"

static struct pci_device_id crc_device_ids[] = {
	{ PCI_DEVICE(CRCDEV_VENDOR_ID, CRCDEV_DEVICE_ID) },
	{ 0 },
};
MODULE_DEVICE_TABLE(pci, crc_device_ids);

static int crc_probe(struct pci_dev *, const struct pci_device_id *);
static void crc_remove(struct pci_dev *);

static struct pci_driver crc_pci_driver = {
	.name = CRCDEV_PCI_NAME,
	.id_table = crc_device_ids,
	.probe = crc_probe,
	.remove = crc_remove,
};

int __must_check crc_pci_init(void) {
	printk(KERN_DEBUG "crcdev: registering PCI driver.");
	return pci_register_driver(&crc_pci_driver);
}

void crc_pci_exit(void) {
	printk(KERN_DEBUG "crcdev: unregistering PCI driver.");
	pci_unregister_driver(&crc_pci_driver);
}

/* UNSAFE */
static void crc_reset_device(struct crc_device *cdev) {
	void __iomem* bar0 = cdev->bar0;
	/* Disable FETCH_DATA and FETCH_CMD */
	iowrite32(0, bar0 + CRCDEV_ENABLE);
	/* Disable interrupts */
	iowrite32(0, bar0 + CRCDEV_INTR_ENABLE);
	/* Set empty FETCH_DATA buffer */
	iowrite32(0, bar0 + CRCDEV_FETCH_DATA_COUNT);
	/* Set empty FETCH_CMD buffer */
	iowrite32(0, bar0 + CRCDEV_FETCH_CMD_READ_POS);
	iowrite32(0, bar0 + CRCDEV_FETCH_CMD_WRITE_POS);
	/* Clear FETCH_DATA interrupt */
	iowrite32(0, bar0 + CRCDEV_FETCH_DATA_INTR_ACK);
}

static int crc_probe(struct pci_dev *pdev, const struct pci_device_id *id) {
	int rv = 0;
	struct crc_device* cdev = NULL;
	printk(KERN_INFO "crcdev: probing PCI device %x:%x:%x.", pdev->vendor,
			pdev->device, pdev->devfn);
	/* We nee this assumption in cleanup function */
	pci_set_drvdata(pdev, NULL);
	if ((rv = pci_enable_device(pdev)))
		goto fail_enable;
	if ((rv = pci_request_regions(pdev, CRCDEV_PCI_NAME)))
		goto fail_request;
	if (!(cdev = crc_device_alloc())) {
		rv = -ENOMEM;
		goto fail;
	}
	pci_set_drvdata(pdev, cdev);
	if (!(cdev->bar0 = pci_iomap(pdev, 0, 0))) {
		rv = -ENODEV;
		goto fail;
	}
	/* This disables interrupts */
	crc_reset_device(cdev);
	/* Device won't try to do anything */
	pci_set_master(pdev);
	if ((rv = pci_set_dma_mask(pdev, DMA_BIT_MASK(CRCDEV_DMA_BITS))))
		goto fail;
	if ((rv = pci_set_consistent_dma_mask(pdev,
					DMA_BIT_MASK(CRCDEV_DMA_BITS))))
		goto fail;
	if ((rv = crc_device_dma_alloc(pdev, cdev)))
		goto fail;
	/* Setup cmd block */
	iowrite32(cdev->cmd_block_dma, cdev->bar0 + CRCDEV_FETCH_CMD_ADDR);
	iowrite32(cdev->cmd_block_len, cdev->bar0 + CRCDEV_FETCH_CMD_SIZE);
	iowrite32(0, cdev->bar0 + CRCDEV_FETCH_CMD_READ_POS);
	iowrite32(0, cdev->bar0 + CRCDEV_FETCH_CMD_WRITE_POS);
	/* Setup interrupts, device is ready and waiting after this step */
	if (pdev->irq == 0) {
		printk(KERN_INFO "crcdev: device cannot generate interrupts");
		goto fail;
	}
	/* Interrupt handlers share crc_device reference with pci module */
	if ((rv = request_irq(pdev->irq, crc_irq_dispatcher, IRQF_SHARED,
					CRCDEV_PCI_NAME, cdev)))
		goto fail;
	set_bit(CRCDEV_STATUS_IRQ, &cdev->status);
	set_bit(CRCDEV_STATUS_READY, &cdev->status);
	/* END CRITICAL (cdev->dev_lock) - no one alive new about our device */
	/* Enable ALL interrupts ATOMICALLY, device will run after this and idle
	 * immediately (there is no pending commands yet) */
	iowrite32(CRCDEV_INTR_ALL, cdev->bar0 + CRCDEV_INTR_ENABLE);
	/* After registering char device someone can use it */
	if ((rv = crc_chrdev_add(pdev, cdev)))
		goto fail;
	if ((rv = crc_sysfs_add(pdev, cdev)))
		goto fail;
	/* Probe scceeded */
	printk(KERN_INFO "crcdev: probed PCI %x:%x:%x.", pdev->vendor,
			pdev->device, pdev->devfn);
	return rv;
fail:
	pci_set_drvdata(pdev, cdev);
	crc_remove(pdev);
	return ERROR(rv);
fail_request:
	pci_disable_device(pdev);
fail_enable:
	return ERROR(rv);
}

// FIXME does this handle every possible path in crc_probe?
// FIXME swicth to kref to handle hot unplug
// FIXME removing must be mutually exclusive with ALL calls
// FIXME removing must wakeup ALL waiting calls before entering remove lock
static void crc_remove(struct pci_dev *pdev) {
	unsigned long flags;
	struct crc_device* cdev = NULL;
	printk(KERN_INFO "crcdev: removing PCI device %x:%x:%x.", pdev->vendor,
			pdev->device, pdev->devfn);
	if (!(cdev = pci_get_drvdata(pdev)))
		goto pci_finalize_remove;
	pci_set_drvdata(pdev, NULL);
	if (!cdev->bar0)
		goto pci_finalize_remove;
	/* START (remove) */
	crc_device_remove_start(cdev);
	/* BEGIN CRITICAL (cdev->dev_lock) */
	spin_lock_irqsave(&cdev->dev_lock, flags);
	/* New interrupts will start to abort from now */
	clear_bit(CRCDEV_STATUS_READY, &cdev->status);
	/* This stops DMA activity and disables interrupts */
	crc_reset_device(cdev);
	spin_unlock_irqrestore(&cdev->dev_lock, flags);
	/* END CRITICAL (cdev->dev_lock) */
	/* Remove from sysfs and unregister char device */
	crc_sysfs_del(pdev, cdev);
	crc_chrdev_del(pdev, cdev);
	/* There is no running interrupt handler after this,
	 * ACHTUNG: doing this under dev_lock causes DEADLOCK */
	if (test_bit(CRCDEV_STATUS_IRQ, &cdev->status))
		free_irq(pdev->irq, cdev);
	clear_bit(CRCDEV_STATUS_IRQ, &cdev->status);
	/* Free DMA memory (this needs irqs), we also need all
	 * tasks to reside in of the queues */
	crc_device_dma_free(pdev, cdev);
	pci_clear_master(pdev);
	/* Unmap memory regions */
	pci_iounmap(pdev, cdev->bar0); cdev->bar0 = NULL;
	crc_device_put(cdev); cdev = NULL;
pci_finalize_remove:
	/* Reordering commented in kernel's Documentation/PCI/pci.txt */
	pci_disable_device(pdev);
	pci_release_regions(pdev);
	printk(KERN_INFO "crcdev: removed PCI device %x:%x:%x.", pdev->vendor,
			pdev->device, pdev->devfn);
}
