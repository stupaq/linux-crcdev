#ifndef MONITORS_H_
#define MONITORS_H_

#include "errors.h"
#include "concepts.h"
#include "pci.h"

static __always_inline __must_check
int __must_check mon_session_call_enter(struct crc_session *sess) {
	int rv = 0;
	struct crc_device *cdev = sess->crc_dev;
	/* BEGIN CRITICAL (sess->call_lock) */
	if ((rv = mutex_lock_interruptible(&sess->call_lock)))
		goto fail_call_lock;
	/* We might have been woken up to die */
	if (test_bit(CRCDEV_STATUS_REMOVED, &cdev->status))
		goto fail_removed_1;
	return rv;
fail_removed_1:
	crc_error_hot_unplug();
	mutex_unlock(&sess->call_lock);
	/* END CRITICAL (sess->call_lock) */
	return -ENODEV;
fail_call_lock:
	return rv;
}

static __always_inline
void mon_session_call_exit(struct crc_session *sess) {
	mutex_unlock(&sess->call_lock);
	/* END CRITICAL (sess->call_lock) */
}

static __always_inline __must_check
int __must_check mon_session_call_devwide_enter(struct crc_device *cdev,
		struct crc_session *sess) {
	int rv;
	/* ENTER (call) */
	if ((rv = mon_session_call_enter(sess)))
		goto fail_call_enter;
	/* BEGIN CRITICAL (cdev->remove_lock) READ */
	if (!down_read_trylock(&cdev->remove_lock))
		goto fail_remove_lock;
	/* We might have been faster than start_remove() */
	if (test_bit(CRCDEV_STATUS_REMOVED, &cdev->status))
		goto fail_removed_2;
	return rv;
fail_removed_2:
	crc_error_hot_unplug();
	up_read(&cdev->remove_lock);
	/* END CRITICAL (cdev->remove_lock) READ */
	mon_session_call_exit(sess);
	/* EXIT (call) */
	return -ENODEV;
fail_remove_lock:
	crc_error_hot_unplug();
	mon_session_call_exit(sess);
	/* EXIT (call) */
	return -ENODEV;
fail_call_enter:
	return rv;
}

static __always_inline
void mon_session_call_devwide_exit(struct crc_device *cdev,
		struct crc_session *sess) {
	up_read(&cdev->remove_lock);
	/* END CRITICAL (cdev->remove_lock) READ */
	mon_session_call_exit(sess);
	/* EXIT (call) */
}

static __always_inline __must_check
int __must_check mon_session_reserve_task(struct crc_session *sess) {
	int rv = 0;
	struct crc_device *cdev = sess->crc_dev;
	if ((rv = down_interruptible(&cdev->free_tasks_wait)))
		goto fail_free_tasks_wait;
	/* We might have been woken up to die */
	if (test_bit(CRCDEV_STATUS_REMOVED, &cdev->status))
		goto fail_removed;
	return rv;
fail_removed:
	crc_error_hot_unplug();
	/* We let another guy know about this */
	up(&cdev->free_tasks_wait);
	return -ENODEV;
fail_free_tasks_wait:
	return rv;
}

static __always_inline
void mon_session_free_task(struct crc_session *sess) {
	struct crc_device *cdev = sess->crc_dev;
	up(&cdev->free_tasks_wait);
}

// TODO unused
static __always_inline __must_check
int mon_device_interrupt_enter(struct crc_device *cdev, unsigned long *flags) {
	/* BEGIN CRITICAL (cdev->dev_lock) */
	spin_lock_irqsave(&cdev->dev_lock, *flags);
	if (test_bit(CRCDEV_STATUS_READY, &cdev->status))
		goto fail_removed;
	return 0;
fail_removed:
	spin_unlock_irqrestore(&cdev->dev_lock, *flags);
	/* END CRITICAL (cdev->dev_lock) */
	return -ENODEV;
}

// TODO unused
static __always_inline
void mon_device_interrupt_exit(struct crc_device *cdev, unsigned long *flags) {
	spin_unlock_irqrestore(&cdev->dev_lock, *flags);
	/* END CRITICAL (cdev->dev_lock) */
}

static __always_inline
void mon_device_ready_start(struct crc_device *cdev) {
	set_bit(CRCDEV_STATUS_IRQ, &cdev->status);
	set_bit(CRCDEV_STATUS_READY, &cdev->status);
	/* END CRITICAL (cdev->dev_lock) - no one alive new about our device */
}

// FIXME prove with new reordering
static __always_inline
void mon_device_remove_start(struct crc_device *cdev) {
	unsigned long flags;
	struct crc_task *task, *tmp;
	/* BEGIN CRITICAL (cdev->dev_lock) */
	spin_lock_irqsave(&cdev->dev_lock, flags);
	/* Interrupts will start to abort from now */
	clear_bit(CRCDEV_STATUS_READY, &cdev->status);
	/* This stops DMA activity and disables interrupts */
	crc_reset_device(cdev->bar0);
	spin_unlock_irqrestore(&cdev->dev_lock, flags);
	/* END CRITICAL (cdev->dev_lock) */

	/* New syscalls and awoken ones will start to fail with -ENODEV */
	set_bit(CRCDEV_STATUS_REMOVED, &cdev->status);
	/* Wakeup all waiting remove_lock holders */
	/* After this up() every process waiting or just-to-be waiting on
	 * free_tasks_wait will spot STATUS_REMOVED flags and reup() the
	 * semaphore, all waiters will wake up sequentially */
	up(&cdev->free_tasks_wait);

	/* Acquire remove_lock, all readers with remmove_lock are woken up and
	 * all locks readers might wait on (free_tasks_wait) are up */
	/* BEGIN CRITICAL (cdev->remove_lock) WRITE */
	down_write(&cdev->remove_lock);
	/* There is no call_devwide from now */

	/* Wakeup all waiting ioctls, to do this we have to complete_all() all
	 * not completed ioctl_comp in sessions. We can't reach all sessions,
	 * but only those who have waiting/scheduled tasks.
	 * REMARK: ioctl_compl is completed `iff` it has no tasks
	 * therefore we can scan waiting and scheduled tasks only */
	/* BEGIN CRITICAL (cdev->dev_lock) */
	spin_lock_irqsave(&cdev->dev_lock, flags);
	list_for_each_entry_safe(task, tmp, &cdev->waiting_tasks, list) {
		complete_all(&task->session->ioctl_comp);
	}
	list_for_each_entry_safe(task, tmp, &cdev->scheduled_tasks, list) {
		complete_all(&task->session->ioctl_comp);
	}
	spin_unlock_irqrestore(&cdev->dev_lock, flags);
	/* END CRITICAL (cdev->dev_lock) */
}

#endif  // MONITORS_H_
