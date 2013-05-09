#ifndef MONITORS_H_
#define MONITORS_H_

#include "errors.h"
#include "concepts.h"
#include "pci.h"

static __always_inline __must_check
int crc_session_call_enter(struct crc_session *sess) {
	int rv = 0;
	struct crc_device *cdev = sess->crc_dev;
	/* BEGIN CRITICAL (sess->call_lock) */
	if ((rv = mutex_lock_interruptible(&sess->call_lock)))
		goto fail_call_lock;
	/* We might have been woken up to die */
	if (test_bit(CRCDEV_STATUS_REMOVED, &cdev->status))
		goto fail_removed_1;
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
	mutex_unlock(&sess->call_lock);
	/* END CRITICAL (sess->call_lock) */
	return -ENODEV;
fail_remove_lock:
	crc_error_hot_unplug();
	mutex_unlock(&sess->call_lock);
	/* END CRITICAL (sess->call_lock) */
	return -ENODEV;
fail_removed_1:
	crc_error_hot_unplug();
	mutex_unlock(&sess->call_lock);
	/* END CRITICAL (sess->call_lock) */
	return -ENODEV;
fail_call_lock:
	return rv;
}

static __always_inline
void crc_session_call_exit(struct crc_session *sess) {
	struct crc_device *cdev = sess->crc_dev;
	up_read(&cdev->remove_lock);
	/* END CRITICAL (cdev->remove_lock) READ */
	mutex_unlock(&sess->call_lock);
	/* END CRITICAL (sess->call_lock) */
}

static __always_inline __must_check
int crc_session_reserve_task(struct crc_session *sess) {
	int rv = 0;
	struct crc_device *cdev = sess->crc_dev;
	/* DOWN (cdev->free_tasks_wait) */
	if ((rv = down_interruptible(&cdev->free_tasks_wait)))
		goto fail_free_tasks_wait;
	/* We might have been woken up to die */
	if (test_bit(CRCDEV_STATUS_REMOVED, &cdev->status))
		goto fail_removed;
	return rv;
fail_removed:
	crc_error_hot_unplug();
	up(&cdev->free_tasks_wait);
	return -ENODEV;
fail_free_tasks_wait:
	return rv;
}

static __always_inline
void crc_session_free_task(struct crc_session *sess) {
	struct crc_device *cdev = sess->crc_dev;
	up(&cdev->free_tasks_wait);
	/* UP (cdev->free_tasks_wait) */
}

static __always_inline
void crc_session_tasks_done(struct crc_session *sess) {
	unsigned long flags;
	spin_lock_irqsave(&sess->sess_lock, flags);
	complete_all(&sess->ioctl_comp);
	spin_unlock_irqrestore(&sess->sess_lock, flags);
}

/* unsafe, must be called when no one waits for completion */
static __always_inline
void crc_session_tasks_new(struct crc_session *sess) {
	unsigned long flags;
	spin_lock_irqsave(&sess->sess_lock, flags);
	INIT_COMPLETION(sess->ioctl_comp);
	spin_unlock_irqrestore(&sess->sess_lock, flags);
}

// TODO unused
static __always_inline
int crc_device_interrupt_enter(struct crc_device *cdev, unsigned long *flags) {
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
void crc_device_interrupt_exit(struct crc_device *cdev, unsigned long *flags) {
	spin_unlock_irqrestore(&cdev->dev_lock, *flags);
	/* END CRITICAL (cdev->dev_lock) */
}

static __always_inline
void crc_device_ready_start(struct crc_device *cdev) {
	set_bit(CRCDEV_STATUS_IRQ, &cdev->status);
	set_bit(CRCDEV_STATUS_READY, &cdev->status);
	/* END CRITICAL (cdev->dev_lock) - no one alive new about our device */
}

// FIXME prove with new reordering
static __always_inline
void crc_device_remove_start(struct crc_device *cdev) {
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
	/* Wakeup all waiting ioctls, to do this we have to complete_all() all
	 * not completed ioctl_comp in sessions. We can't reach all sessions,
	 * but only those who have waiting/scheduled tasks.
	 * REMARK: ioctl_compl is completed `iff` it has no tasks
	 * therefore we can scan waiting and scheduled tasks only */
	/* BEGIN CRITICAL (cdev->dev_lock) */
	spin_lock_irqsave(&cdev->dev_lock, flags);
	list_for_each_entry_safe(task, tmp, &cdev->waiting_tasks, list) {
		crc_session_tasks_done(task->session);
	}
	list_for_each_entry_safe(task, tmp, &cdev->scheduled_tasks, list) {
		crc_session_tasks_done(task->session);
	}
	spin_unlock_irqrestore(&cdev->dev_lock, flags);
	/* END CRITICAL (cdev->dev_lock) */
	/* Wakeup all waiting writes, they will spot STATUS_REMOVED and die */
	// FIXME
	/* DOWN (cdev->remove_lock) WRITE */
	down_write(&cdev->remove_lock);
}

#endif  // MONITORS_H_
