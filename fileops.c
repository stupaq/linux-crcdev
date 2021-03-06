#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include "crcdev_ioctl.h"
#include "fileops.h"
#include "concepts.h"
#include "monitors.h"
#include "interrupts.h"

MODULE_LICENSE("GPL");

static int crc_fileops_open(struct inode *inode, struct file *filp) {
	struct crc_session *sess;
	unsigned minor = iminor(inode);
	struct crc_device *cdev = crc_device_get(minor);
	filp->private_data = NULL;
	if (cdev == NULL || cdev != container_of(inode->i_cdev, struct
				crc_device, char_dev))
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
	int rv = 0;
	struct crc_device *cdev;
	struct crc_session *sess;
	/* Note that there are no other syscalls to this session, it can be
	 * reference by irq handler, remove device code and us */
	if ((sess = filp->private_data)) {
		cdev = sess->crc_dev;
		/* Wait for all tasks to complete, tasks's session pointer must
		 * stay valid since it will be dereferenced by irq handler */
		rv = mon_session_tasks_wait(sess);
		crc_session_free(sess); sess = NULL;
		crc_device_put(cdev); cdev = NULL;
	}
	/* IGNORE (rv) */
	return 0;
}

/* Note that write and ioctl are serialized using session->call_lock */
static ssize_t crc_fileops_write(struct file *filp, const char __user *buff,
		size_t lcount, loff_t *offp) {
	int rv;
	struct crc_session *sess = filp->private_data;
	struct crc_device *cdev = sess->crc_dev;
	unsigned long flags;
	struct crc_task *task;
	size_t written = 0, to_copy;
	/* ENTER (call_devwide) */
	if ((rv = mon_session_call_devwide_enter(cdev, sess)))
		goto fail_call_devwide_enter;
	while (lcount > 0) {
		if ((rv = mon_session_reserve_task(sess)))
			goto fail_reserve_task;
		/* We know that there is a task for us (we can take only one) */
		/* BEGIN CRITICAL (cdev->dev_lock) */
		mon_device_lock(cdev, flags);
		BUG_ON(list_empty(&cdev->free_tasks));
		task = list_first_entry(&cdev->free_tasks, struct crc_task,
				list);
		list_del(&task->list);
		mon_device_unlock(cdev, flags);
		/* END CRITICAL (cdev->dev_lock) */
		/* Acquired block must be returned to either free_tasks or
		 * waiting_tasks before we leave CRITICAL (call_devwide) */
		task->session = sess;
		/* This may sleep */
		to_copy = min(lcount, (size_t) CRCDEV_BUFFER_SIZE);
		if (copy_from_user(task->data, buff, to_copy))
			goto fail_copy;
		task->data_count = to_copy;
		lcount -= to_copy;
		buff += to_copy;
		*offp += to_copy;
		written += to_copy;
		/* BEGIN CRITICAL (cdev->dev_lock) */
		mon_device_lock(cdev, flags);
		/* There is no concurrent ioctl nor remove has started, we have
		 * locked interrupts, no one will wait or complete ioctl_comp */
		INIT_COMPLETION(sess->ioctl_comp);
		list_add_tail(&task->list, &cdev->waiting_tasks);
		sess->waiting_count++;
		crc_irq_enable(cdev);
		mon_device_unlock(cdev, flags);
		/* END CRITICAL (cdev->dev_lock) */
	}
	mon_session_call_devwide_exit(cdev, sess);
	/* EXIT (call_devwide) */
	return written;
fail_copy:
	/* Return acquired block */
	/* BEGIN CRITICAL (cdev->dev_lock) */
	mon_device_lock(cdev, flags);
	list_add(&task->list, &cdev->free_tasks);
	mon_session_free_task(sess);
	mon_device_unlock(cdev, flags);
	/* END CRITICAL (cdev->dev_lock) */
	mon_session_call_devwide_exit(cdev, sess);
	/* EXIT (call_devwide) */
	return -EFAULT;
fail_reserve_task:
	mon_session_call_devwide_exit(cdev, sess);
	/* EXIT (call_devwide) */
	/* Report error (or signal) only if haven't written anything */
	if (written == 0) return rv;
	else return written;
fail_call_devwide_enter:
	/* We've done nothing so far */
	return rv;
}

/* CRITICAL (call) */
static int crc_ioctl_set_params(struct crc_session *sess, void __user * argp) {
	struct crcdev_ioctl_set_params params = { 0, 0 };
	if (copy_from_user(&params, argp, sizeof(params)))
		return -EFAULT;
	sess->poly = params.poly;
	sess->sum = params.sum;
	my_debug("set_params: poly %x sum %x", params.poly, params.sum);
	return 0;
}

/* CRITICAL (call) */
static int crc_ioctl_get_result(struct crc_session *sess, void __user * argp) {
	struct crcdev_ioctl_get_result result = { 0 };
	result.sum = sess->sum;
	my_debug("get_result: sum %x", result.sum);
	if (copy_to_user(argp, &result, sizeof(result)))
		return -EFAULT;
	return 0;
}

static long crc_fileops_ioctl(struct file *filp, unsigned int cmd, unsigned long
		arg) {
	int rv;
	void __user *argp = (__force void __user *) arg;
	struct crc_session *sess = filp->private_data;
	/* ENTER (call) */
	if ((rv = mon_session_call_enter(sess)))
		goto fail_call_enter;
	/* Wait for all tasks to complete, there is no concurrent write (no one
	 * can reinitialize this completion) */
	if ((rv = mon_session_tasks_wait_interruptible(sess)))
		goto fail_ioctl_comp;
	/* There are no waiting tasks nor concurrent write */
	switch (cmd) {
	case CRCDEV_IOCTL_SET_PARAMS:
		rv = crc_ioctl_set_params(sess, argp);
		break;
	case CRCDEV_IOCTL_GET_RESULT:
		rv = crc_ioctl_get_result(sess, argp);
		break;
	default:
		printk(KERN_WARNING "crcdev: unrecognized ioctl %u", cmd);
		rv = -ENOTTY;
	}
	mon_session_call_exit(sess);
	/* EXIT (call) */
	return rv;
fail_ioctl_comp:
	mon_session_call_exit(sess);
	/* EXIT (call) */
fail_call_enter:
	return rv;
}

struct file_operations crc_fileops_fops = {
	.owner = THIS_MODULE,
	.open = crc_fileops_open,
	.release = crc_fileops_release,
	.write = crc_fileops_write,
	.unlocked_ioctl = crc_fileops_ioctl,
	.compat_ioctl = crc_fileops_ioctl,
	/* We do not support llseek */
	.llseek = no_llseek,
};
