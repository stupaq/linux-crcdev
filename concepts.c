#include <linux/kernel.h>
#include <linux/pci.h>
#include "concepts.h"

struct crc_device * __must_check crc_device_alloc(void) {
	struct crc_device *cdev;
	if ((cdev = kzalloc(sizeof(*cdev), GFP_KERNEL))) {
		INIT_LIST_HEAD(&cdev->free_tasks);
		INIT_LIST_HEAD(&cdev->waiting_tasks);
		INIT_LIST_HEAD(&cdev->scheduled_tasks);
	}
	return cdev;
}

void crc_device_free(struct crc_device *cdev) {
	kfree(cdev);
}

int __must_check crc_device_dma_alloc(struct device *pdev,
		struct crc_device *cdev) {
	int i;
	struct crc_task *task;
	cdev->cmd_block_len = CRCDEV_CMDS_COUNT;
	cdev->cmd_block = dma_alloc_coherent(pdev, sizeof(*(cdev->cmd_block)) *
			cdev->cmd_block_len, &cdev->cmd_block_dma, GFP_KERNEL);
	if (!cdev->cmd_block)
		goto fail;
	for (i = 0; i < CRCDEV_BUFFERS_COUNT; i++) {
		if (!(task = kzalloc(sizeof(*task), GFP_KERNEL)))
			goto fail;
		task->data_sz = CRCDEV_BUFFER_SIZE;
		task->data = dma_alloc_coherent(pdev, task->data_sz,
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

void crc_device_dma_free(struct device *pdev, struct crc_device *cdev) {
	struct crc_task *task, *tmp;
	if (cdev->cmd_block) {
		dma_free_coherent(pdev, sizeof(*cdev->cmd_block) *
				cdev->cmd_block_len, cdev->cmd_block,
				cdev->cmd_block_dma);
	}
	list_for_each_entry_safe(task, tmp, &cdev->free_tasks, list) {
		dma_free_coherent(pdev, task->data_sz, task->data,
				task->data_dma);
		kfree(task);
	}
}
