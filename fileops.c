#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include "crcdev_ioctl.h"
#include "fileops.h"
#include "concepts.h"

static int crc_fileops_open(struct inode *inode, struct file *filp) {
	struct crc_device *cdev = container_of(inode->i_cdev, struct crc_device,
			char_dev);
	// TODO
	filp->private_data = NULL;
	return 0;
}

static int crc_fileops_release(struct inode *inode, struct file *filp) {
	struct crc_session *sess = filp->private_data;
	// TODO
	return 0;
}

static ssize_t crc_fileops_write(struct file *filp, const char __user *buff,
		size_t count, loff_t *offp) {
	struct crc_session *sess = filp->private_data;
	// TODO
	return -EFAULT;
}

/* ioctl(CRCDEV_IOCTL_SET_PARAMS) affects crc computation in undefined way if
 * called after write but before ioctl(CRCDEV_IOCTL_GET_RESULT) */
static int crc_ioctl_set_params(struct crc_session *sess, void __user * argp) {
	int rv = 0;
	struct crcdev_ioctl_set_params params;
	if (copy_from_user(&params, argp, sizeof(params))) {
		rv = -EFAULT;
		goto exit;
	}
	// TODO
exit:
	return rv;
}

static int crc_ioctl_get_result(struct crc_session *sess, void __user * argp) {
	int rv = 0;
	struct crcdev_ioctl_get_result result;
	result.sum = 42;
	// TODO
	if (copy_to_user(argp, &result, sizeof(result))) {
		rv = -EFAULT;
		goto exit;
	}
exit:
	return rv;
}

static long crc_fileops_ioctl(struct file *filp, unsigned int cmd, unsigned long
		arg) {
	void __user *argp = (__force void __user *) arg;
	struct crc_session *sess = filp->private_data;
	switch (cmd) {
	case CRCDEV_IOCTL_SET_PARAMS: return crc_ioctl_set_params(sess, argp);
	case CRCDEV_IOCTL_GET_RESULT: return crc_ioctl_get_result(sess, argp);
	default: return -EINVAL;
	}
}

struct file_operations crc_fileops_fops = {
	.owner = THIS_MODULE,
	.open = crc_fileops_open,
	.release = crc_fileops_release,
	.write = crc_fileops_write,
	// FIXME can set both to the same handler?
	.unlocked_ioctl = crc_fileops_ioctl,
	.compat_ioctl = crc_fileops_ioctl,
	/* We do not support llseek */
	.llseek = no_llseek,
};
