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
#define	CRCDEV_DEVS_COUNT	255
#define	CRCDEV_BASE_MINOR	0

/* Common */
int __must_check crc_concepts_init(void);
void crc_concepts_exit(void);

/* crc_session */
struct crc_session {
	size_t waiting_count;
	/* context: */
	size_t scheduled_count;
	u32 poly;
	u32 sum;
};

/* crc_task */
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

/* crc_device */
#define	CRCDEV_STATUS_IRQ	0x00000001
#define	CRCDEV_STATUS_READY	0x00000002
#define	CRCDEV_STATUS_CHRDEV	0x00000004

struct crc_device {
	unsigned long status;			// dev_lock(rw)
	/* Locks */
	spinlock_t dev_lock;
	/* Tasks for this device */
	struct list_head free_tasks;		// dev_lock(rw)
	struct list_head waiting_tasks;		// dev_lock(rw)
	struct list_head scheduled_tasks;	// dev_lock(rw)
	/* BAR0 address */
	void __iomem *bar0;			// dev_lock(rw)
	/* Number of entries in cmd_block */
	size_t cmd_block_len;			// init
	/* Address of first cmd_block entry in dev address space */
	dma_addr_t cmd_block_dma;		// init
	struct crc_command *cmd_block;		// init
	/* Sysfs device */
	struct device *sysfs_dev;		// init
	/* Char dev and its minor number */
	struct cdev char_dev;			// init
	unsigned int minor;			// init
	/* Reference counting, module is the initial owner of crc_device */
	struct kref refc;			// private
};

struct crc_device * __must_check crc_device_alloc(void);

struct crc_device * __must_check crc_device_get(unsigned int);
void crc_device_put(struct crc_device *);

int __must_check crc_device_dma_alloc(struct pci_dev *, struct crc_device *);
void crc_device_dma_free(struct pci_dev *, struct crc_device *);

#endif  /* SESSION_H_ */
