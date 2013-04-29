#include <linux/kernel.h>
#include <linux/module.h>

#include "pci.h"
#include "cexcept.h"

MODULE_AUTHOR("Mateusz Machalica");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("crcdev driver");

/* We use cleanup_crcdev if init fails, therefore no __exit annotation */
static void cleanup_crcdev(void)
{
	printk(KERN_DEBUG "crcdev: unloading crcdev module.");
	crc_pci_exit();
}

static int __init init_crcdev(void)
{
	int rv = 0;
	printk(KERN_DEBUG "crcdev: loading crcdev module.");
	if ((rv = crc_pci_init()) < 0)
		goto fail;

	return rv;
fail:
	cleanup_crcdev();
	return ERROR(rv);
}

module_init(init_crcdev);
module_exit(cleanup_crcdev);
