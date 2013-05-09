#ifndef SESSION_H_
#define SESSION_H_

#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/completion.h>
#include <linux/rwsem.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include "crcdev.h"

#define	CRCDEV_CMDS_COUNT	32
#define	CRCDEV_BUFFERS_COUNT	4 // TODO
#define	CRCDEV_BUFFER_SIZE	4096 // TODO
#define	CRCDEV_DEVS_COUNT	255
#define	CRCDEV_BASE_MINOR	0

struct crc_device;

/* Common */
int __must_check crc_concepts_init(void);
void crc_concepts_exit(void);

/* crc_session */
struct crc_session {
	struct crc_device *crc_dev;
	/* Locks */
	spinlock_t sess_lock;
	struct mutex call_lock;
	/* Complete iff waiting_count + scheduled_count == 0 */
	struct completion ioctl_comp;		// sess_lock(lu)
	/* Task stats */
	size_t waiting_count;			// sess_lock(rw)
	size_t scheduled_count;			// sess_lock(rw)
	/* Context */
	u32 poly;				// sess_lock(rw)
	u32 sum;				// sess_lock(rw)
};

struct crc_session * __must_check crc_session_alloc(struct crc_device *);
void crc_session_free(struct crc_session *);

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

static __always_inline
void crc_task_attach(struct crc_session *sess, struct crc_task *task) {
	task->session = sess;
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
#define	CRCDEV_STATUS_IRQ	1
#define	CRCDEV_STATUS_READY	2
#define	CRCDEV_STATUS_CHRDEV	4
#define	CRCDEV_STATUS_REMOVED	8

struct crc_device {
	volatile unsigned long status;		// atomic bitops
	/* Locks */
	spinlock_t dev_lock;
	struct rw_semaphore remove_lock; /* no reader will ever wait */
	struct semaphore free_tasks_wait;
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
	struct crc_command *cmd_block;		// dev_lock(rw)
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
