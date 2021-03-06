#ifndef MONITORS_H_
#define MONITORS_H_

#include "concepts.h"
#include "pci.h"

/** SYNCHRONIZATION SCHEMA:
 * mon_device_{lock,unlock}
 * - short period locking (spinlock) of crc_device data structure
 * - not necessary for reading status flags
 * - prevents interrupt handler from servicing this device
 * mon_device_ready_start
 * - invoked after successful initialization of a device (before first task)
 * mon_device_remove_start
 * - starts device removal procedure, after this call neither interrupt handler
 *   nor syscall would try to use device
 * mon_session_call_{enter,exit}
 * - serialization of syscalls, one cannot use device directly or acquire
 *   session_call_devwide
 * mon_session_call_devwide_{enter,exit}
 * - serialization of syscalls, one is guaranteed that device will not be
 *   removed until he leaves this monitor
 * - one cannot acquire plain session_call
 * mon_session_reserve_task
 * - grants a permission to obtain one free task and put it in waiting tasks
 *   queue (at the end), one is guaranteed that there is a task waiting for him
 * mon_session_free_task
 * - signals that there is a newly added free task in free tasks queue
 * mon_session_tasks_wait*
 * - waits for completion of all scheduled tasks, cannot be called when one
 *   acquired session_call_devwide
 * SAFE SCENARIOS:
 * session_call > session_tasks_wait (ioctl)
 * session_call_devwide > session_reserve_task > device_lock (write)
 * device_lock (irq handler)
 * session_tasks_wait (release)
 **/

#define crc_error_hot_unplug() printk(KERN_WARNING \
		"crcdev: device removed while there was pending syscall")

#define mon_device_lock(cdev, flags) \
	spin_lock_irqsave(&(cdev)->dev_lock, flags);
#define mon_device_unlock(cdev, flags) \
	spin_unlock_irqrestore(&(cdev)->dev_lock, flags);

static __always_inline __must_check
int __must_check mon_session_call_enter(struct crc_session *sess) {
	int rv;
	struct crc_device *cdev = sess->crc_dev;
	/* BEGIN CRITICAL (sess->call_lock) */
	if ((rv = mutex_lock_interruptible(&sess->call_lock)))
		goto fail_call_lock;
	/* We might have been woken up to die */
	if (test_bit(CRCDEV_STATUS_REMOVED, &cdev->status))
		goto fail_removed_1;
	return rv;
fail_removed_1:
	mutex_unlock(&sess->call_lock);
	/* END CRITICAL (sess->call_lock) */
	crc_error_hot_unplug();
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
	up_read(&cdev->remove_lock);
	/* END CRITICAL (cdev->remove_lock) READ */
fail_remove_lock:
	mon_session_call_exit(sess);
	/* EXIT (call) */
	crc_error_hot_unplug();
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
	/* We let another guy know about this */
	up(&cdev->free_tasks_wait);
	crc_error_hot_unplug();
	return -ENODEV;
fail_free_tasks_wait:
	return rv;
}

static __always_inline
void mon_session_free_task(struct crc_session *sess) {
	up(&sess->crc_dev->free_tasks_wait);
}

static __always_inline __must_check
int mon_session_tasks_wait_interruptible(struct crc_session *sess) {
	int rv;
	if ((rv = wait_for_completion_interruptible(&sess->ioctl_comp))) {
		if (rv == -ERESTARTSYS)
			return -EINTR;
		return rv;
	}
	if (test_bit(CRCDEV_STATUS_REMOVED, &sess->crc_dev->status)) {
		crc_error_hot_unplug();
		return -ENODEV;
	}
	WARN_ON(sess->scheduled_count > 0);
	WARN_ON(sess->waiting_count > 0);
	return 0;
}

static __always_inline __must_check
int mon_session_tasks_wait(struct crc_session *sess) {
	wait_for_completion(&sess->ioctl_comp);
	if (test_bit(CRCDEV_STATUS_REMOVED, &sess->crc_dev->status)) {
		crc_error_hot_unplug();
		return -ENODEV;
	}
	WARN_ON(sess->scheduled_count > 0);
	WARN_ON(sess->waiting_count > 0);
	return 0;
}

static __always_inline
void mon_device_ready_start(struct crc_device *cdev) {
	unsigned long flags;
	/* BEGIN CRITICAL (cdev->dev_lock) */
	mon_device_lock(cdev, flags);
	set_bit(CRCDEV_STATUS_READY, &cdev->status);
	mon_device_unlock(cdev, flags);
	/* END CRITICAL (cdev->dev_lock) */
}

static __always_inline
void mon_device_remove_start(struct crc_device *cdev) {
	unsigned long flags;
	struct crc_task *task, *tmp;
	/* BEGIN CRITICAL (cdev->dev_lock) */
	mon_device_lock(cdev, flags);
	/* Interrupts will start to abort from now */
	clear_bit(CRCDEV_STATUS_READY, &cdev->status);
	/* New syscalls and awoken ones will start to fail with -ENODEV */
	set_bit(CRCDEV_STATUS_REMOVED, &cdev->status);
	/* This stops DMA activity and disables interrupts */
	crc_reset_device(cdev->bar0);
	mon_device_unlock(cdev, flags);
	/* END CRITICAL (cdev->dev_lock) */

	/* Wakeup all waiting remove_lock holders, every process waiting or
	 * just-to-be waiting on free_tasks_wait will spot STATUS_REMOVED flags
	 * and reup() the semaphore, all waiters will wake up sequentially */
	up(&cdev->free_tasks_wait);

	/* Acquire remove_lock, all readers with remmove_lock are woken up and
	 * all locks readers might wait on (free_tasks_wait) are up */
	/* BEGIN CRITICAL (cdev->remove_lock) WRITE */
	down_write(&cdev->remove_lock);
	/* There is no call_devwide from now */

	/* Wakeup all waiting ioctls, to do this we have to complete_all() all
	 * not completed ioctl_comp in sessions. We can't reach all sessions,
	 * but only those who have waiting/scheduled tasks.
	 * REMARK: ioctl_compl is completed `iff` session has no tasks
	 * therefore we can scan waiting and scheduled tasks only */
	/* BEGIN CRITICAL (cdev->dev_lock) */
	mon_device_lock(cdev, flags);
	list_for_each_entry_safe(task, tmp, &cdev->waiting_tasks, list) {
		complete_all(&task->session->ioctl_comp);
	}
	list_for_each_entry_safe(task, tmp, &cdev->scheduled_tasks, list) {
		complete_all(&task->session->ioctl_comp);
	}
	mon_device_unlock(cdev, flags);
	/* END CRITICAL (cdev->dev_lock) */
}

#endif  // MONITORS_H_
