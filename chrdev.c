#include <linux/cdev.h>
#include <linux/fs.h>
#include "chrdev.h"
#include "pci.h"

static unsigned int crc_chrdev_major = 0;
static int crc_chrdev_init_success = 0;

int __must_check crc_chrdev_init(void) {
	int rv = 0;
	dev_t dev = MKDEV(crc_chrdev_major, 0);
	if ((rv = alloc_chrdev_region(&dev, CRCDEV_BASE_MINOR,
					CRCDEV_DEVS_COUNT, CRCDEV_PCI_NAME)))
		return rv;
	crc_chrdev_major = MAJOR(dev);
	crc_chrdev_init_success = 1;
	printk(KERN_INFO "crcdev: chrdev major: %d", crc_chrdev_major);
	return rv;
}

void crc_chrdev_exit(void) {
	if (crc_chrdev_init_success) {
		unregister_chrdev_region(MKDEV(crc_chrdev_major,
					CRCDEV_BASE_MINOR), CRCDEV_DEVS_COUNT);
		crc_chrdev_init_success = 0;
	}
}

dev_t crc_chrdev_getdev(struct crc_device *cdev) {
	return MKDEV(crc_chrdev_major, cdev->minor);
}

int __must_check crc_chrdev_add(struct pci_dev *pdev, struct crc_device *cdev) {
	int rv = 0;
	// TODO
	return rv;
}

void crc_chrdev_del(struct pci_dev *pdev, struct crc_device *cdev) {
	// TODO
}
