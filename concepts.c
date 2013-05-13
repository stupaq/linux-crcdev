#include <asm/atomic.h>
#include "concepts.h"
#include "monitors.h"
#include "chrdev.h"

MODULE_LICENSE("GPL");

/* GC statistics */
static struct {
	atomic_t devices;
	atomic_t sessions;
	atomic_t tasks;
	atomic_t dma_blocks;
} crc_gc;

/* crc_session */
struct crc_session * __must_check crc_session_alloc(struct crc_device *cdev) {
	struct crc_session *sess;
	if ((sess = kzalloc(sizeof(*sess), GFP_KERNEL))) {
		atomic_inc(&crc_gc.sessions);
		sess->crc_dev = cdev;
		mutex_init(&sess->call_lock);
		init_completion(&sess->ioctl_comp);
		complete_all(&sess->ioctl_comp);
		sess->ctx = CRCDEV_SESSION_NOCTX;
	}
	return sess;
}

void crc_session_free(struct crc_session *sess) {
	if (!sess) return;
	kfree(sess); sess = NULL;
	atomic_dec(&crc_gc.sessions);
}

/* crc_device */
static DECLARE_BITMAP(crc_device_minors, CRCDEV_DEVS_COUNT);
static struct crc_device *crc_device_minors_mapping[CRCDEV_DEVS_COUNT];
static DEFINE_MUTEX(crc_device_minors_lock);

struct crc_device * __must_check crc_device_alloc(void) {
	int idx;
	struct crc_device *cdev;
	/* Create device structure */
	if (!(cdev = kzalloc(sizeof(*cdev), GFP_KERNEL)))
		goto fail_alloc;
	atomic_inc(&crc_gc.devices);
	/* Obtain minor */
	mutex_lock(&crc_device_minors_lock);
	idx = find_first_zero_bit(crc_device_minors, CRCDEV_DEVS_COUNT);
	if (0 <= idx && idx < CRCDEV_DEVS_COUNT) {
		set_bit(idx, crc_device_minors);
		crc_device_minors_mapping[idx] = cdev;
	}
	mutex_unlock(&crc_device_minors_lock);
	if (idx < 0 || CRCDEV_DEVS_COUNT <= idx)
		goto fail_minor;
	/* Locks */
	spin_lock_init(&cdev->dev_lock);
	init_rwsem(&cdev->remove_lock);
	sema_init(&cdev->free_tasks_wait, 0);
	/* Contexts */
	bitmap_zero(cdev->contexts_map, CRCDEV_CTX_COUNT);
	/* Task lists */
	INIT_LIST_HEAD(&cdev->free_tasks);
	INIT_LIST_HEAD(&cdev->waiting_tasks);
	INIT_LIST_HEAD(&cdev->scheduled_tasks);
	/* Minor */
	cdev->minor = CRCDEV_BASE_MINOR + idx;
	/* Reference counting */
	kref_init(&cdev->refc);
	return cdev;
fail_minor:
	kfree(cdev); cdev = NULL;
	atomic_dec(&crc_gc.devices);
fail_alloc:
	return NULL;
}

/* UNSAFE */
static void crc_device_free_kref(struct kref *ref) {
	struct crc_device *cdev = container_of(ref, struct crc_device, refc);
	int idx = cdev->minor - CRCDEV_BASE_MINOR;
	/* Relese minor */
	clear_bit(idx, crc_device_minors);
	crc_device_minors_mapping[idx] = NULL;
	/* Free mem */
	kfree(cdev); cdev = NULL;
	atomic_dec(&crc_gc.devices);
}

/* Note that the initial reference to crc_device is held by pci module,
 * these functions are only used by open/release file operations */
struct crc_device * __must_check crc_device_get(unsigned int minor) {
	int idx = minor - CRCDEV_BASE_MINOR;
	struct crc_device *cdev;
	mutex_lock(&crc_device_minors_lock);
	if ((cdev = crc_device_minors_mapping[idx])) {
		kref_get(&cdev->refc);
	}
	mutex_unlock(&crc_device_minors_lock);
	return cdev;
}

void crc_device_put(struct crc_device *cdev) {
	if (!cdev) return;
	mutex_lock(&crc_device_minors_lock);
	kref_put(&cdev->refc, crc_device_free_kref);
	mutex_unlock(&crc_device_minors_lock);
}

/* init_only, sleeps */
int __must_check crc_device_dma_alloc(struct pci_dev *pdev,
		struct crc_device *cdev) {
	int count;
	struct crc_task *task;
	BUILD_BUG_ON(sizeof(*(cdev->cmd_block)) != CRCDEV_CMD_SIZE);
	cdev->cmd_block = dma_alloc_coherent(&pdev->dev,
			sizeof(*(cdev->cmd_block)) * CRCDEV_COMMANDS_LENGTH,
			&cdev->cmd_block_dma, GFP_KERNEL);
	if (!cdev->cmd_block)
		goto fail;
	atomic_inc(&crc_gc.dma_blocks);
	for (count = 0; count < CRCDEV_BUFFERS_COUNT; count++) {
		// TODO deal with smaller number of blocks too
		if (!(task = kzalloc(sizeof(*task), GFP_KERNEL)))
			goto fail;
		atomic_inc(&crc_gc.tasks);
		task->data = dma_alloc_coherent(&pdev->dev, CRCDEV_BUFFER_SIZE,
				&task->data_dma, GFP_KERNEL);
		if (!task->data) {
			/* Free partially created task */
			kfree(task); task = NULL;
			atomic_dec(&crc_gc.tasks);
			goto fail;
		}
		atomic_inc(&crc_gc.dma_blocks);
		list_add(&task->list, &cdev->free_tasks);
	}
	sema_init(&cdev->free_tasks_wait, count);
	return 0;
fail:
	crc_device_dma_free(pdev, cdev);
	return -ENOMEM;
}

/* deinit_only, sleeps */
void crc_device_dma_free(struct pci_dev *pdev, struct crc_device *cdev) {
	unsigned long flags;
	struct crc_task *task, *tmp;
	struct list_head tmp_list;
	if (cdev->cmd_block) {
		dma_free_coherent(&pdev->dev, sizeof(*cdev->cmd_block) *
				CRCDEV_COMMANDS_LENGTH, cdev->cmd_block,
				cdev->cmd_block_dma);
		atomic_dec(&crc_gc.dma_blocks);
		cdev->cmd_block = NULL;
	}
	INIT_LIST_HEAD(&tmp_list);
	/* BEGIN CRITICAL (cdev->dev_lock) */
	mon_device_lock(cdev, flags);
	list_splice_init(&cdev->free_tasks, &tmp_list);
	list_splice_init(&cdev->waiting_tasks, &tmp_list);
	list_splice_init(&cdev->scheduled_tasks, &tmp_list);
	mon_device_unlock(cdev, flags);
	/* END CRITICAL (cdev->dev_lock) */
	list_for_each_entry_safe(task, tmp, &tmp_list, list) {
		dma_free_coherent(&pdev->dev, CRCDEV_BUFFER_SIZE, task->data,
				task->data_dma);
		atomic_dec(&crc_gc.dma_blocks);
		kfree(task); task = NULL;
		atomic_dec(&crc_gc.tasks);
	}
}

/* Common */
int crc_concepts_init(void) {
	/* Minor allocation */
	bitmap_zero(crc_device_minors, CRCDEV_DEVS_COUNT);
	/* Initialize GC stats */
	atomic_set(&crc_gc.devices, 0);
	atomic_set(&crc_gc.sessions, 0);
	atomic_set(&crc_gc.tasks, 0);
	atomic_set(&crc_gc.dma_blocks, 0);
	return 0;
}

void crc_concepts_exit(void) {
	int devices = atomic_read(&crc_gc.devices),
	    sessions = atomic_read(&crc_gc.sessions),
	    tasks = atomic_read(&crc_gc.tasks),
	    dma_blocks = atomic_read(&crc_gc.dma_blocks);
	if (devices || sessions || tasks || dma_blocks) {
		printk(KERN_ERR "crcdev: concepts: not all objects collected: "
			"devices %d, sessions %d, tasks %d, dma_blocks %d",
			devices, sessions, tasks, dma_blocks);
	} else {
		printk(KERN_INFO "crcdev: concepts: gc successful");
	}
}
