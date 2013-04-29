#include <linux/kernel.h>
#include "concepts.h"

// TODO replace with dma_pool/slab allocators

struct crc_device * __must_check crc_device_alloc(struct pci_dev *dev) {
	dma_addr_t dma_handle;
	struct crc_device *cdev = dma_alloc_coherent((struct device *) dev,
			sizeof(struct crc_device), &dma_handle, GFP_KERNEL);
	if (cdev) {
		memset(cdev, sizeof(struct crc_device), 0);
		cdev->cmd_block_len = CRCDEV_CMD_BLOCK_LENGTH;
		cdev->cmd_block_dma = dma_handle + offsetof(struct crc_device,
				cmd_block);
	}
	return cdev;
}

void crc_device_free(struct pci_dev *pdev, struct crc_device *cdev) {
	dma_free_coherent((struct device *) pdev, sizeof(struct crc_device),
			cdev, cdev->cmd_block_dma - offsetof(struct crc_device,
				cmd_block));
}
