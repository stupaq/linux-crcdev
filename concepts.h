#ifndef SESSION_H_
#define SESSION_H_

#include <asm/types.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/cdev.h>
#include "crcdev.h"

#define	CRCDEV_CMDS_COUNT	32
#define	CRCDEV_BUFFERS_COUNT	32
#define	CRCDEV_BUFFER_SIZE	4096

struct crc_session {
	size_t waiting_count;
	/* context: */
	size_t scheduled_count;
	u32 poly;
	u32 sum;
};

struct crc_task {
	/* One task can be in one of the following: scheduled, waiting, free */
	struct list_head list;
	/* Session which this task belongs to */
	struct crc_session *session;
	/* Address of data in device's address space */
	size_t data_sz;
	dma_addr_t data_dma;
	u8 *data;
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

#define	CRCDEV_STATUS_IRQ   0x00000001
#define	CRCDEV_STATUS_READY 0x00000002

struct crc_device {
	unsigned long status;
	/* Locks */
	spinlock_t dev_lock;
	/* Tasks for this device */
	struct list_head free_tasks;
	struct list_head waiting_tasks;
	struct list_head scheduled_tasks;
	/* BAR0 address */
	void __iomem *bar0;
	/* Number of entries in cmd_block */
	size_t cmd_block_len;
	/* Address of first cmd_block entry in dev address space */
	dma_addr_t cmd_block_dma;
	struct crc_command *cmd_block;
	/* Sysfs device */
	struct device *sysfs_dev;
	/* Char dev and its minor number, minor >= CRCDEV_DEVS_COUNT +
	 * CRCDEV_BASE_MINOR  means no char device was added */
	struct cdev char_dev;
	unsigned int minor;
	/* Reference counting, module is the initial owner of crc_device */
	struct kref refc;
};

struct crc_device *crc_device_alloc(void);
void crc_device_free(struct crc_device *);

int crc_device_dma_alloc(struct pci_dev *, struct crc_device *);
void crc_device_dma_free(struct pci_dev *, struct crc_device *);

#endif  /* SESSION_H_ */
