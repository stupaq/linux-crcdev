#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/compiler.h>
#include "errors.h"
#include "chrdev.h"
#include "pci.h"

static unsigned int crc_chrdev_major = 0;
static int crc_chrdev_init_success = 0;
static DECLARE_BITMAP(crc_chrdev_minors, CRCDEV_DEVS_COUNT);
static DEFINE_MUTEX(crc_chrdev_minors_lock);

int __must_check crc_chrdev_init(void) {
	int rv = 0;
	dev_t dev = MKDEV(crc_chrdev_major, 0);
	if ((rv = alloc_chrdev_region(&dev, CRCDEV_BASE_MINOR,
					CRCDEV_DEVS_COUNT, CRCDEV_PCI_NAME)))
		return rv;
	crc_chrdev_major = MAJOR(dev);
	crc_chrdev_init_success = 1;
	bitmap_zero(crc_chrdev_minors, CRCDEV_DEVS_COUNT);
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
	int idx;
	mutex_lock(&crc_chrdev_minors_lock);
	if ((idx = bitmap_find_free_region(crc_chrdev_minors,
					CRCDEV_DEVS_COUNT, 0)) >= 0)
		bitmap_allocate_region(crc_chrdev_minors, idx, 0);
	mutex_unlock(&crc_chrdev_minors_lock);
	if (0 <= idx && idx < CRCDEV_DEVS_COUNT) {
		cdev_init(&dev->char_dev, NULL); // FIXME
		dev->char_dev.owner = THIS_MODULE;
		dev->minor = (unsigned int) idx + CRCDEV_BASE_MINOR;
		if ((rv = cdev_add(&dev->char_dev, crc_chrdev_getdev(dev), 1)))
			goto fail_add;
	} else {
		dev->minor = CRCDEV_BASE_MINOR + CRCDEV_DEVS_COUNT;
		rv = -ENOMEM; // FIXME what to do here?
	}
	return rv;
fail_add:
	idx = dev->minor - CRCDEV_BASE_MINOR;
	mutex_lock(&crc_chrdev_minors_lock);
	bitmap_release_region(crc_chrdev_minors, idx, 0);
	mutex_unlock(&crc_chrdev_minors_lock);
	dev->minor = CRCDEV_BASE_MINOR + CRCDEV_DEVS_COUNT;
	return ERROR(rv);
}

void crc_chrdev_del(struct pci_dev *pdev, struct crc_device *dev) {
	int idx = dev->minor - CRCDEV_BASE_MINOR;
	if (0 <= idx && idx < CRCDEV_DEVS_COUNT) {
		cdev_del(&dev->char_dev);
		/* Relese minor after removing device */
		mutex_lock(&crc_chrdev_minors_lock);
		bitmap_release_region(crc_chrdev_minors, idx, 0);
		mutex_unlock(&crc_chrdev_minors_lock);
		dev->minor = CRCDEV_BASE_MINOR + CRCDEV_DEVS_COUNT;
	}
}
