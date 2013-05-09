#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include "crcdev_ioctl.h"
#include "fileops.h"
#include "concepts.h"
#include "monitors.h"

static int crc_fileops_open(struct inode *inode, struct file *filp) {
	struct crc_session *sess;
	unsigned minor = iminor(inode);
	struct crc_device *cdev = crc_device_get(minor);
	if (cdev != container_of(inode->i_cdev, struct crc_device, char_dev))
		goto fail_dev;
	if (!(sess = crc_session_alloc(cdev)))
		goto fail_session;
	filp->private_data = sess;
	return 0;
fail_session:
	crc_device_put(cdev); cdev = NULL;
	return -ENOMEM;
fail_dev:
	crc_device_put(cdev); cdev = NULL;
	return -ENODEV;
}

// FIXME concurrent write/ioctl and release?
static int crc_fileops_release(struct inode *inode, struct file *filp) {
	struct crc_device *cdev;
	struct crc_session *sess;
	if ((sess = filp->private_data)) {
		cdev = sess->crc_dev;
		// FIXME wait for all tasks to complete
		crc_session_free(sess); sess = NULL;
		crc_device_put(cdev); cdev = NULL;
	}
	return 0;
}

/* Note that write and ioctl are serialized using session->call_lock */
static ssize_t crc_fileops_write(struct file *filp, const char __user *buff,
		size_t count, loff_t *offp) {
	int rv = 0;
	struct crc_session *sess = filp->private_data;
	struct crc_device *cdev = sess->crc_dev;
	unsigned long flags;
	struct crc_task *task;
	/* ENTER (call) */
	if ((rv = crc_session_call_enter(sess)))
		goto fail_call_enter;
	// FIXME while (count > 0) {
		if ((rv = crc_session_reserve_task(sess)))
			goto fail_reserve_task;
		/* We know that there is a task for us (we can take only one) */
		/* BEGIN CRITICAL (cdev->dev_lock) */
		spin_lock_irqsave(&cdev->dev_lock, flags);
		task = list_first_entry(&cdev->free_tasks, struct crc_task,
				list);
		list_del(&task->list);
		spin_unlock_irqrestore(&cdev->dev_lock, flags);
		/* END CRITICAL (cdev->dev_lock) */
		// TODO
		/* There is no concurrent ioctl/write for the same session */
		// FIXME lock ioctl
		/* BEGIN CRITICAL (cdev->dev_lock) */
		spin_lock_irqsave(&cdev->dev_lock, flags);
		list_add_tail(&task->list, &cdev->waiting_tasks);
		spin_unlock_irqrestore(&cdev->dev_lock, flags);
		/* END CRITICAL (cdev->dev_lock) */
	// FIXME }
	crc_session_call_exit(sess);
	/* EXIT (call) */
	// TODO
	return -EFAULT;
fail_reserve_task:
	crc_session_call_exit(sess);
	/* EXIT (call) */
	return rv;
fail_call_enter:
	return rv;
}

/* ioctl(CRCDEV_IOCTL_SET_PARAMS) affects crc computation in an undefined way if
 * called after write but before ioctl(CRCDEV_IOCTL_GET_RESULT) */
/* CRITICAL (call) */
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

/* CRITICAL (call) */
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
	int rv;
	void __user *argp = (__force void __user *) arg;
	struct crc_session *sess = filp->private_data;
	/* ENTER (call) */
	if ((rv = crc_session_call_enter(sess)))
		goto fail_call_enter;
	switch (cmd) {
	case CRCDEV_IOCTL_SET_PARAMS:
		rv = crc_ioctl_set_params(sess, argp);
		break;
	case CRCDEV_IOCTL_GET_RESULT:
		rv = crc_ioctl_get_result(sess, argp);
		break;
	default:
		rv = -EINVAL;
	}
	crc_session_call_exit(sess);
	/* EXIT (call) */
	return rv;
fail_call_enter:
	return rv;
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
