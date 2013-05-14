#include <linux/pci.h>
#include "crcdev.h"
#include "pci.h"
#include "concepts.h"
#include "interrupts.h"
#include "chrdev.h"
#include "sysfs.h"
#include "monitors.h"

MODULE_LICENSE("GPL");

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

/* unsafe */
void crc_reset_device(void __iomem* bar0) {
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
	mmiowb();
}

/* unsafe */
static void crc_prepare_fetch_cmd(struct crc_device *cdev) {
	iowrite32(cdev->cmd_block_dma, cdev->bar0 + CRCDEV_FETCH_CMD_ADDR);
	/* Just like the initial value of  cdev->next_pos */
	iowrite32(cdev->next_pos, cdev->bar0 + CRCDEV_FETCH_CMD_READ_POS);
	iowrite32(cdev->next_pos, cdev->bar0 + CRCDEV_FETCH_CMD_WRITE_POS);
	/* This is one more than actual number of cmds that can fit in */
	iowrite32(CRCDEV_COMMANDS_LENGTH, cdev->bar0 + CRCDEV_FETCH_CMD_SIZE);
	/* Enable fetch cmd and fetch data (there are no commands) */
	iowrite32(CRCDEV_ENABLE_FETCH_DATA | CRCDEV_ENABLE_FETCH_CMD,
			cdev->bar0 + CRCDEV_ENABLE);
	mmiowb();
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
	crc_reset_device(cdev->bar0);
	/* Device won't try to do anything */
	pci_set_master(pdev);
	if ((rv = pci_set_dma_mask(pdev, DMA_BIT_MASK(CRCDEV_DMA_BITS))))
		goto fail;
	if ((rv = pci_set_consistent_dma_mask(pdev,
					DMA_BIT_MASK(CRCDEV_DMA_BITS))))
		goto fail;
	if ((rv = crc_device_dma_alloc(pdev, cdev)))
		goto fail;
	/* Setup interrupts, device is ready and waiting after this step */
	if (pdev->irq == 0) {
		printk(KERN_INFO "crcdev: device cannot generate interrupts");
		rv = -ENODEV;
		goto fail;
	}
	/* Interrupt handlers share crc_device reference with pci module */
	if ((rv = request_irq(pdev->irq, crc_irq_dispatcher, IRQF_SHARED,
					CRCDEV_PCI_NAME, cdev)))
		goto fail;
	set_bit(CRCDEV_STATUS_IRQ, &cdev->status);
	/* Setup cmd block */
	crc_prepare_fetch_cmd(cdev);
	/* START (ready) */
	mon_device_ready_start(cdev);
	/* Enable interrupts, device will run after this and idle immediately
	 * (there is no pending commands yet) */
	crc_irq_enable(cdev);
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
	return rv;
fail_request:
	/* This is refcounted, we won't screw up other sessions with device */
	pci_disable_device(pdev);
	return rv;
fail_enable:
	return rv;
}

static void crc_remove(struct pci_dev *pdev) {
	struct crc_device* cdev = NULL;
	printk(KERN_INFO "crcdev: removing PCI device %x:%x:%x.", pdev->vendor,
			pdev->device, pdev->devfn);
	if (!(cdev = pci_get_drvdata(pdev)))
		goto pci_finalize_remove;
	pci_set_drvdata(pdev, NULL);
	if (!cdev->bar0)
		goto pci_remove_device;
	/* START (remove) */
	mon_device_remove_start(cdev);
	/* Remove from sysfs and unregister char device */
	crc_sysfs_del(pdev, cdev);
	crc_chrdev_del(pdev, cdev);
	/* There is no running interrupt handler after this,
	 * ACHTUNG: doing this under dev_lock causes DEADLOCK */
	if (test_bit(CRCDEV_STATUS_IRQ, &cdev->status))
		free_irq(pdev->irq, cdev);
	clear_bit(CRCDEV_STATUS_IRQ, &cdev->status);
	/* Free DMA memory (this needs irqs), we also need all
	 * tasks to reside in one of the queues */
	crc_device_dma_free(pdev, cdev);
	pci_clear_master(pdev);
	/* Unmap memory regions */
	pci_iounmap(pdev, cdev->bar0); cdev->bar0 = NULL;
pci_remove_device:
	crc_device_put(cdev); cdev = NULL;
pci_finalize_remove:
	/* Reordering commented in kernel's Documentation/PCI/pci.txt */
	pci_disable_device(pdev);
	pci_release_regions(pdev);
	printk(KERN_INFO "crcdev: removed PCI device %x:%x:%x.", pdev->vendor,
			pdev->device, pdev->devfn);
}
