#include <linux/pci.h>

#include "crcdev.h"
#include "pci.h"

static struct pci_device_id crcdev_device_ids[] = {
	{ PCI_DEVICE(CRCDEV_VENDOR_ID, CRCDEV_DEVICE_ID) },
	{ 0 },
};
MODULE_DEVICE_TABLE(pci, crcdev_device_ids);

static int crcdev_probe(struct pci_dev *dev, const struct pci_device_id *id);
static void crcdev_remove(struct pci_dev *dev);

static struct pci_driver crcdev_pci_driver = {
	.name = CRCDEV_PCI_NAME,
	.id_table = crcdev_device_ids,
	.probe = crcdev_probe,
	.remove = crcdev_remove,
};

static int crcdev_probe(struct pci_dev *dev, const struct pci_device_id *id) {
	int retval = 0;
	printk(KERN_INFO "crcdev: probing PCI device %x:%x:%x.",
			dev->vendor,
			dev->device, dev->devfn);
	// TODO
	return retval;
}

static void crcdev_remove(struct pci_dev *dev) {
	printk(KERN_INFO "crcdev: removing PCI device %x:%x:%x.", dev->vendor,
			dev->device, dev->devfn);
	// TODO
}

int crcdev_pci_init(void) {
	printk(KERN_DEBUG "crcdev: registering PCI driver.");
	return pci_register_driver(&crcdev_pci_driver);
}

void crcdev_pci_exit(void) {
	printk(KERN_DEBUG "crcdev: unregistering PCI driver.");
	pci_unregister_driver(&crcdev_pci_driver);
}
