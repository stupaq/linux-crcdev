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

static int crc_fileops_release(struct inode *inode, struct file *filp) {
	struct crc_device *cdev;
	struct crc_session *sess;
	/* Note that there are no other syscalls to this session, it can be
	 * reference by irq handler, remove device code and us */
	if ((sess = filp->private_data)) {
		cdev = sess->crc_dev;
		/* Wait for all tasks to complete, tasks's session pointer must
		 * stay valid since it will be dereferenced by irq handler */
		wait_for_completion(&sess->ioctl_comp);
		// TODO consider failing here
		crc_session_free(sess); sess = NULL;
		crc_device_put(cdev); cdev = NULL;
	}
	return 0;
}

/* Note that write and ioctl are serialized using session->call_lock */
static ssize_t crc_fileops_write(struct file *filp, const char __user *buff,
		size_t count, loff_t *offp) {
	int rv;
	struct crc_session *sess = filp->private_data;
	struct crc_device *cdev = sess->crc_dev;
	unsigned long flags;
	struct crc_task *task;
	size_t written = 0, max_copy;
	/* ENTER (call_devwide) */
	if ((rv = mon_session_call_devwide_enter(cdev, sess)))
		goto fail_call_devwide_enter;
	while (count > written) {
		if ((rv = mon_session_reserve_task(sess)))
			goto fail_reserve_task;
		/* We know that there is a task for us (we can take only one) */
		/* BEGIN CRITICAL (cdev->dev_lock) */
		spin_lock_irqsave(&cdev->dev_lock, flags);
		WARN_ON(list_empty(&cdev->free_tasks));
		task = list_first_entry(&cdev->free_tasks, struct crc_task,
				list);
		list_del(&task->list);
		spin_unlock_irqrestore(&cdev->dev_lock, flags);
		/* END CRITICAL (cdev->dev_lock) */
		crc_task_attach(sess, task);
		/* This may sleep */
		max_copy = (count > task->data_sz) ? task->data_sz : count;
		if (copy_from_user(task->data, buff, max_copy))
			goto fail_copy;
		written += max_copy;
		*offp += max_copy;
		/* BEGIN CRITICAL (cdev->dev_lock) */
		spin_lock_irqsave(&cdev->dev_lock, flags);
		/* There is no concurrent ioctl nor remove has started, we have
		 * locked interrupts, no one will wait or complete ioctl_comp */
		INIT_COMPLETION(sess->ioctl_comp);
		list_add_tail(&task->list, &cdev->waiting_tasks);
		sess->waiting_count++;
		spin_unlock_irqrestore(&cdev->dev_lock, flags);
		/* END CRITICAL (cdev->dev_lock) */
	}
	WARN_ON(written > count);
	mon_session_call_devwide_exit(cdev, sess);
	/* EXIT (call_devwide) */
	return written;
fail_copy:
	mon_session_call_devwide_exit(cdev, sess);
	/* EXIT (call_devwide) */
	return -EFAULT;
fail_reserve_task:
	mon_session_call_devwide_exit(cdev, sess);
	/* EXIT (call_devwide) */
	// FIXME what to return when written > 0 and -EINTR?
	return rv;
fail_call_devwide_enter:
	// FIXME what to return when written > 0 and -EINTR?
	return rv;
}

/* ioctl(CRCDEV_IOCTL_SET_PARAMS) affects crc computation in an undefined way if
 * called after write but before ioctl(CRCDEV_IOCTL_GET_RESULT) */
/* CRITICAL (call) */
static int crc_ioctl_set_params(struct crc_session *sess, void __user * argp) {
	struct crcdev_ioctl_set_params params;
	if (copy_from_user(&params, argp, sizeof(params)))
		goto fail_copy;
	/* This can corrupt computation */
	sess->poly = params.sum;
	sess->sum = params.sum;
	return 0;
fail_copy:
	return -EFAULT;
}

/* CRITICAL (call) */
static int crc_ioctl_get_result(struct crc_session *sess, void __user * argp) {
	int rv;
	struct crcdev_ioctl_get_result result;
	/* Wait for all tasks to complete, there is no concurrent write (no one
	 * can reinitialize this completion) */
	if ((rv = wait_for_completion_interruptible(&sess->ioctl_comp)))
		goto fail_ioctl_comp;
	// TODO consider failing here
	WARN_ON(sess->scheduled_count > 0);
	WARN_ON(sess->waiting_count > 0);
	/* There is no waiting tasks nor concurrent write */
	result.sum = sess->sum;
	if (copy_to_user(argp, &result, sizeof(result)))
		goto fail_copy;
	return rv;
fail_copy:
	return -EFAULT;
fail_ioctl_comp:
	return rv;
}

static long crc_fileops_ioctl(struct file *filp, unsigned int cmd, unsigned long
		arg) {
	int rv;
	void __user *argp = (__force void __user *) arg;
	struct crc_session *sess = filp->private_data;
	/* ENTER (call) */
	if ((rv = mon_session_call_enter(sess)))
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
	mon_session_call_exit(sess);
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
