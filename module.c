#include <linux/kernel.h>
#include <linux/module.h>
#include "errors.h"
#include "chrdev.h"
#include "sysfs.h"
#include "pci.h"

MODULE_AUTHOR("Mateusz Machalica");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("crcdev driver");

/* We use cleanup_crcdev if init fails, therefore no __exit annotation */
static void cleanup_crcdev(void)
{
	printk(KERN_DEBUG "crcdev: unloading crcdev module.");
	crc_pci_exit();
	crc_sysfs_exit();
	crc_chrdev_exit();
}

static int __init init_crcdev(void)
{
	int rv = 0;
	printk(KERN_DEBUG "crcdev: loading crcdev module.");
	if ((rv = crc_chrdev_init()))
		goto fail;
	if ((rv = crc_sysfs_init()))
		goto fail;
	if ((rv = crc_pci_init()))
		goto fail;

	return rv;
fail:
	cleanup_crcdev();
	return ERROR(rv);
}

module_init(init_crcdev);
module_exit(cleanup_crcdev);
