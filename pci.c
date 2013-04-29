#include <linux/pci.h>

#include "crcdev.h"
#include "pci.h"
#include "cexcept.h"
#include "concepts.h"

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

int crc_pci_init(void) {
	printk(KERN_DEBUG "crcdev: registering PCI driver.");
	return pci_register_driver(&crc_pci_driver);
}

void crc_pci_exit(void) {
	printk(KERN_DEBUG "crcdev: unregistering PCI driver.");
	pci_unregister_driver(&crc_pci_driver);
}

int __must_check crc_pci_intline(struct pci_dev *pdev) {
	int rv = 0;
	u8 line;
	if ((rv = pci_read_config_byte(pdev, PCI_INTERRUPT_LINE, &line)) < 0)
		return ERROR(rv);
	return line;
}

static int crc_reset_device(struct crc_device *cdev) {
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
	return 0;
}

static int crc_probe(struct pci_dev *pdev, const struct pci_device_id *id) {
	int rv = 0;
	struct crc_device* cdev = NULL;
	printk(KERN_INFO "crcdev: probing PCI device %x:%x:%x.", pdev->vendor,
			pdev->device, pdev->devfn);
	/* We nee this assumption in cleanup function */
	pci_set_drvdata(pdev, NULL);
	if ((rv = pci_enable_device(pdev)) < 0)
		goto fail_enable;
	if ((rv = pci_request_regions(pdev, CRCDEV_PCI_NAME)) < 0)
		goto fail_request;
	if (!(cdev = kzalloc(sizeof(struct crc_device), GFP_KERNEL))) {
		rv = -ENOMEM;
		goto fail;
	}
	pci_set_drvdata(pdev, cdev);
	if (!(cdev->bar0 = pci_iomap(pdev, CRCDEV_BAR0, 0))) {
		rv = -ENODEV;
		goto fail;
	}
	if ((rv = crc_reset_device(cdev)) < 0)
		goto fail;
	pci_set_master(pdev);
	if (pci_set_dma_mask(pdev, DMA_BIT_MASK(CRCDEV_DMABITS)) < 0)
		goto fail;
	if (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(CRCDEV_DMABITS)) < 0)
		goto fail;
	// TODO
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

static void crc_remove(struct pci_dev *pdev) {
	struct crc_device* cdev = NULL;
	printk(KERN_INFO "crcdev: removing PCI device %x:%x:%x.", pdev->vendor,
			pdev->device, pdev->devfn);
	if ((cdev = pci_get_drvdata(pdev))) {
		if (cdev->bar0) {
			/* This stops DMA activity and disables interrupts */
			crc_reset_device(cdev);
			// TODO
			pci_clear_master(pdev); // FIXME is this necessary?
			pci_iounmap(pdev, cdev->bar0);
		}
		pci_set_drvdata(pdev, NULL);
		kfree(cdev);
	}
	/* Reordering commented in kernel's Documentation/PCI/pci.txt */
	pci_disable_device(pdev);
	pci_release_regions(pdev);
}
