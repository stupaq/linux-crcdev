#ifndef MONITORS_H_
#define MONITORS_H_

#include "concepts.h"

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
	up_read(&cdev->remove_lock);
	mutex_unlock(&sess->call_lock);
	return -ENODEV;
fail_remove_lock:
	mutex_unlock(&sess->call_lock);
	return -ENODEV;
fail_removed_1:
	mutex_unlock(&sess->call_lock);
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
fail_removed:
	up(&cdev->free_tasks_wait);
	return rv;
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
void crc_device_remove_start(struct crc_device *cdev) {
	/* New syscalls will start to fail with -ENODEV from now */
	set_bit(CRCDEV_STATUS_REMOVED, &cdev->status);
	/* Note that there might be some calls waiting for free tasks and
	 * holding cdev->remove_lock, we have to wake up all of them, so that
	 * they will spot STATUS_REMOVED and free cdev->remove_lock.
	 * Calls acquiring cdev->remove_lock just before us will exit too */
	// FIXME
	/* BEGIN CRITICAL (cdev->remove_lock) WRITE */
	down_write(&cdev->remove_lock);
}

#endif  // MONITORS_H_
