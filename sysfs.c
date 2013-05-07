#include <linux/device.h>
#include <linux/module.h>
#include <linux/err.h>
#include "chrdev.h"
#include "sysfs.h"
#include "errors.h"

static struct class *crc_sysfs_class = NULL;

int __must_check crc_sysfs_init(void) {
	int rv = 0;
	crc_sysfs_class = class_create(THIS_MODULE, CRCDEV_CLASS_NAME);
	if (IS_ERR_OR_NULL(crc_sysfs_class)) { // FIXME is null valid here?
		rv = ERROR(PTR_ERR(crc_sysfs_class));
		crc_sysfs_class = NULL;
	}
	return rv;
}

void crc_sysfs_exit(void) {
	if (crc_sysfs_class) { // FIXME is null valid here?
		class_destroy(crc_sysfs_class);
		crc_sysfs_class = NULL;
	}
}

int __must_check crc_sysfs_add(struct pci_dev *pdev, struct crc_device *cdev) {
	int rv = 0;
	dev_t dev = crc_chrdev_getdev(cdev);
	cdev->sysfs_dev = device_create(crc_sysfs_class, &pdev->dev, dev, cdev,
			"crc%d", cdev->minor);
	if (IS_ERR_OR_NULL(cdev->sysfs_dev)) { // FIXME is null valid here?
		rv = ERROR(PTR_ERR(cdev->sysfs_dev));
		cdev->sysfs_dev = NULL;
	}
	return rv;
}

void crc_sysfs_del(struct pci_dev *pdev, struct crc_device *cdev) {
	if (cdev->sysfs_dev) { // FIXME is null valid here?
		device_destroy(crc_sysfs_class, crc_chrdev_getdev(cdev));
	}
}
