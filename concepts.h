#ifndef SESSION_H_
#define SESSION_H_

#include <asm/types.h>
#include <linux/pci.h>
#include <linux/list.h>
#include "crcdev.h"

#define	CRCDEV_CMD_BLOCK_LENGTH 32
#define	CRCDEV_BUFS_PER_DEV	32

struct crc_session {
	size_t waiting_count;
	/* context: */
	size_t scheduled_count;
	u32 poly;
	u32 sum;
};

struct crc_task {
	struct crc_session *session;
	struct list_head list;
	/* Address of data in device's address space */
	size_t data_size;
	dma_addr_t data_dma;
	void *data;
};

struct crc_command {
	u32 __bitwise addr;
	u32 __bitwise count_ctx;
};

static __always_inline void
crc_command_set_count(struct crc_command *cmd, u32 count) {
	cmd->count_ctx = (cmd->count_ctx & ~CRCDEV_CMD_COUNT_MASK)
		| (count & CRCDEV_CMD_COUNT_MASK);
}

static __always_inline void
crc_command_set_ctx(struct crc_command *cmd, u8 ctx) {
	cmd->count_ctx = (cmd->count_ctx & CRCDEV_CMD_COUNT_MASK)
		| (ctx & ~CRCDEV_CMD_COUNT_MASK);
}

struct crc_device {
	void __iomem *bar0;
	size_t cmd_block_len;
	struct list_head free_tasks;
	/* Address of cmd_block in device's address space */
	dma_addr_t cmd_block_dma;
	struct crc_command cmd_block[CRCDEV_CMD_BLOCK_LENGTH];
};

struct crc_device * __must_check crc_device_alloc(struct pci_dev *);
void crc_device_free(struct pci_dev *, struct crc_device *);

#endif  /* SESSION_H_ */
