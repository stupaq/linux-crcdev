#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/compiler.h>
#include "errors.h"
#include "pci.h"
#include "chrdev.h"
#include "fileops.h"

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

int __must_check crc_chrdev_add(struct pci_dev *pdev, struct crc_device *dev) {
	int rv = 0;
	cdev_init(&dev->char_dev, &crc_fileops_fops);
	dev->char_dev.owner = THIS_MODULE;
	if ((rv = cdev_add(&dev->char_dev, crc_chrdev_getdev(dev), 1)))
		goto fail_add;
	set_bit(CRCDEV_STATUS_CHRDEV, &dev->status);
	return rv;
fail_add:
	return ERROR(rv);
}

void crc_chrdev_del(struct pci_dev *pdev, struct crc_device *dev) {
	if (test_bit(CRCDEV_STATUS_CHRDEV, &dev->status)) {
		cdev_del(&dev->char_dev);
		clear_bit(CRCDEV_STATUS_CHRDEV, &dev->status);
	}
}
