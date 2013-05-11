#include <linux/device.h>
#include <linux/module.h>
#include <linux/err.h>
#include "chrdev.h"
#include "sysfs.h"

MODULE_LICENSE("GPL");

static struct class *crc_sysfs_class = NULL;

int __must_check crc_sysfs_init(void) {
	int rv = 0;
	crc_sysfs_class = class_create(THIS_MODULE, CRCDEV_CLASS_NAME);
	/* Read the source, this can't be null is everything is OK */
	if (!crc_sysfs_class) {
		rv = -ENOMEM;
	} else if (IS_ERR(crc_sysfs_class)) {
		rv = PTR_ERR(crc_sysfs_class);
		crc_sysfs_class = NULL;
	}
	return rv;
}

void crc_sysfs_exit(void) {
	if (crc_sysfs_class) {
		class_destroy(crc_sysfs_class);
		crc_sysfs_class = NULL;
	}
}

int __must_check crc_sysfs_add(struct pci_dev *pdev, struct crc_device *cdev) {
	int rv = 0;
	dev_t dev = crc_chrdev_getdev(cdev);
	cdev->sysfs_dev = device_create(crc_sysfs_class, &pdev->dev, dev, cdev,
			"crc%d", cdev->minor);
	/* Read the source, this can't be null is everything is OK */
	if (!cdev->sysfs_dev) {
		rv = -ENOMEM;
	} else if (IS_ERR(cdev->sysfs_dev)) {
		rv = PTR_ERR(cdev->sysfs_dev);
		cdev->sysfs_dev = NULL;
	}
	return rv;
}

void crc_sysfs_del(struct pci_dev *pdev, struct crc_device *cdev) {
	if (cdev->sysfs_dev) {
		device_destroy(crc_sysfs_class, crc_chrdev_getdev(cdev));
	}
}
