#ifndef SESSION_H_
#define SESSION_H_

#include <asm/types.h>

#include "crcdev.h"

#define	CRCDEV_CMD_BLOCK_LENGTH 128

struct crc_session {
	size_t waiting_count;
	/* context: */
	size_t scheduled_count;
	u32 poly;
	u32 sum;
};

struct crc_task {
	struct crc_session *session;
	/* block: */
	size_t data_count;
	void *cpu_addr;
	dma_addr_t dma_handle;
};

struct crc_command {
	u32 addr;
	u32 count_ctx;
};

struct crc_device {
	void __iomem *bar0;
	size_t cmd_block_length;
	struct crc_command *cmd_block;
};

void __always_inline crc_command_set_count(struct crc_command *cmd, u32 count) {
	cmd->count_ctx = (cmd->count_ctx & ~CRCDEV_CMD_COUNT_MASK)
		| (count & CRCDEV_CMD_COUNT_MASK);
}

void __always_inline crc_command_set_ctx(struct crc_command *cmd, u8 ctx) {
	cmd->count_ctx = (cmd->count_ctx & CRCDEV_CMD_COUNT_MASK)
		| (ctx & ~CRCDEV_CMD_COUNT_MASK);
}

#endif  /* SESSION_H_ */
