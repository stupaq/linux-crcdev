#include <linux/kernel.h>
#include <linux/module.h>
#include "chrdev.h"
#include "sysfs.h"
#include "pci.h"

MODULE_AUTHOR("Mateusz Machalica");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("crcdev driver");

static void __exit cleanup_crcdev(void)
{
	printk(KERN_DEBUG "crcdev: unloading crcdev module.");
	crc_pci_exit();
	crc_sysfs_exit();
	crc_chrdev_exit();
	crc_concepts_exit();
}

static int __init init_crcdev(void)
{
	int rv = 0;
	printk(KERN_DEBUG "crcdev: loading crcdev module.");
	if ((rv = crc_concepts_init()))
		goto fail_concepts;
	if ((rv = crc_chrdev_init()))
		goto fail_chrdev;
	if ((rv = crc_sysfs_init()))
		goto fail_sysfs;
	if ((rv = crc_pci_init()))
		goto fail_pci;
	return rv;
fail_pci:
	crc_sysfs_exit();
fail_sysfs:
	crc_chrdev_exit();
fail_chrdev:
	crc_concepts_exit();
fail_concepts:
	return rv;
}

module_init(init_crcdev);
module_exit(cleanup_crcdev);
