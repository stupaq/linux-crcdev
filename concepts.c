#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include "concepts.h"
#include "chrdev.h"

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
	kfree(cdev);
	return NULL;
}

static void crc_device_free(struct crc_device *cdev) {
	int idx = cdev->minor - CRCDEV_BASE_MINOR;
	/* Relese minor after removing device */
	mutex_lock(&crc_device_minors_lock);
	bitmap_release_region(crc_device_minors, idx, 0);
	crc_device_minors_mapping[idx] = NULL;
	mutex_unlock(&crc_device_minors_lock);
	/* Free mem */
	kfree(cdev);
}

void crc_device_free_kref(struct kref *ref) {
	crc_device_free(container_of(ref, struct crc_device, refc));
}

/* Note that the initial reference to crc_device is held by pci module,
 * these functions are only used by open/release file operations */
struct crc_device * __must_check crc_device_get(int minor) {
	// FIXME
	return NULL;
}

void crc_device_put(int minor) {
	// FIXME
}

int __must_check crc_device_dma_alloc(struct pci_dev *pdev,
		struct crc_device *cdev) {
	int i;
	struct crc_task *task;
	cdev->cmd_block_len = CRCDEV_CMDS_COUNT;
	cdev->cmd_block = dma_alloc_coherent(&pdev->dev,
			sizeof(*(cdev->cmd_block)) * cdev->cmd_block_len,
			&cdev->cmd_block_dma, GFP_KERNEL);
	if (!cdev->cmd_block)
		goto fail;
	for (i = 0; i < CRCDEV_BUFFERS_COUNT; i++) {
		if (!(task = kzalloc(sizeof(*task), GFP_KERNEL)))
			goto fail;
		task->data_sz = CRCDEV_BUFFER_SIZE;
		task->data = dma_alloc_coherent(&pdev->dev, task->data_sz,
				&task->data_dma, GFP_KERNEL);
		if (!task->data) {
			/* Free partially created task */
			kfree(task);
			goto fail;
		}
		list_add(&task->list, &cdev->free_tasks);
	}
	return 0;
fail:
	crc_device_dma_free(pdev, cdev);
	return -ENOMEM;
}

void crc_device_dma_free(struct pci_dev *pdev, struct crc_device *cdev) {
	struct crc_task *task, *tmp;
	if (cdev->cmd_block) {
		dma_free_coherent(&pdev->dev, sizeof(*cdev->cmd_block) *
				cdev->cmd_block_len, cdev->cmd_block,
				cdev->cmd_block_dma);
	}
	list_for_each_entry_safe(task, tmp, &cdev->free_tasks, list) {
		dma_free_coherent(&pdev->dev, task->data_sz, task->data,
				task->data_dma);
		kfree(task);
	}
}

/* Common */
int crc_concepts_init(void) {
	bitmap_zero(crc_device_minors, CRCDEV_DEVS_COUNT);
	return 0;
}

void crc_concepts_exit(void) {
}
