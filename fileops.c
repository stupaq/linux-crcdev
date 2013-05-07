#include <linux/module.h>
#include "fileops.h"

struct file_operations crc_fileops_fops = {
	.owner = THIS_MODULE,
};
