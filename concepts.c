#include "concepts.h"
#include "monitors.h"
#include "chrdev.h"

MODULE_LICENSE("GPL");

/* crc_session */
struct crc_session * __must_check crc_session_alloc(struct crc_device *cdev) {
	struct crc_session *sess;
	if ((sess = kzalloc(sizeof(*sess), GFP_KERNEL))) {
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
	/* Obtain minor */
	mutex_lock(&crc_device_minors_lock);
	// FIXME bitops are just enough
	idx = bitmap_find_free_region(crc_device_minors, CRCDEV_DEVS_COUNT, 0);
	if (idx >= 0) {
		bitmap_allocate_region(crc_device_minors, idx, 0);
		crc_device_minors_mapping[idx] = cdev;
	}
	mutex_unlock(&crc_device_minors_lock);
	if (idx < 0)
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
fail_alloc:
	return NULL;
fail_minor:
	kfree(cdev); cdev = NULL;
	return NULL;
}

/* UNSAFE */
static void crc_device_free_kref(struct kref *ref) {
	struct crc_device *cdev = container_of(ref, struct crc_device, refc);
	int idx = cdev->minor - CRCDEV_BASE_MINOR;
	/* Relese minor after removing device */
	bitmap_release_region(crc_device_minors, idx, 0);
	crc_device_minors_mapping[idx] = NULL;
	/* Free mem */
	kfree(cdev); cdev = NULL;
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
	int idx;
	if (!cdev) return;
	idx = cdev->minor - CRCDEV_BASE_MINOR;
	mutex_lock(&crc_device_minors_lock);
	kref_put(&cdev->refc, crc_device_free_kref);
	mutex_unlock(&crc_device_minors_lock);
}

/* init_only, sleeps */
int __must_check crc_device_dma_alloc(struct pci_dev *pdev,
		struct crc_device *cdev) {
	int count;
	struct crc_task *task;
	cdev->cmd_block_len = CRCDEV_CMDS_COUNT;
	cdev->cmd_block = dma_alloc_coherent(&pdev->dev,
			sizeof(*(cdev->cmd_block)) * cdev->cmd_block_len,
			&cdev->cmd_block_dma, GFP_KERNEL);
	if (!cdev->cmd_block)
		goto fail;
	for (count = 0; count < CRCDEV_BUFFERS_COUNT; count++) {
		// FIXME deal with smaller number of blocks too
		if (!(task = kzalloc(sizeof(*task), GFP_KERNEL)))
			goto fail;
		task->data_sz = CRCDEV_BUFFER_SIZE;
		task->data = dma_alloc_coherent(&pdev->dev, task->data_sz,
				&task->data_dma, GFP_KERNEL);
		if (!task->data) {
			/* Free partially created task */
			kfree(task); task = NULL;
			goto fail;
		}
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
				cdev->cmd_block_len, cdev->cmd_block,
				cdev->cmd_block_dma);
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
	// FIXME deal with smaller number of blocks too
	list_for_each_entry_safe(task, tmp, &tmp_list, list) {
		dma_free_coherent(&pdev->dev, task->data_sz, task->data,
				task->data_dma);
		kfree(task); task = NULL;
	}
}

/* Common */
int crc_concepts_init(void) {
	bitmap_zero(crc_device_minors, CRCDEV_DEVS_COUNT);
	return 0;
}

void crc_concepts_exit(void) {
	/* nop */
}
