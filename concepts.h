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
#include <asm/page.h>
#include "crcdev.h"

#ifdef CRC_DEBUG
#define my_debug(fmt, args...) printk(KERN_DEBUG "crcdev: " fmt, ## args)
#else
#define my_debug(fmt, args...)
#endif  // CRC_DEBUG

// TODO deal with less blocks than exact count
#define	CRCDEV_BUFFERS_LOWER	8
#define	CRCDEV_BUFFERS_COUNT	24
#define	CRCDEV_COMMANDS_LENGTH	(CRCDEV_BUFFERS_COUNT + 1)
#define	CRCDEV_BUFFER_SIZE	(PAGE_SIZE * 4)
#define	CRCDEV_DEVS_COUNT	255
#define	CRCDEV_BASE_MINOR	0

struct crc_device;

/* Common */
int __must_check crc_concepts_init(void);
void crc_concepts_exit(void);

/* crc_session */
#define CRCDEV_SESSION_NOCTX	(-1)

struct crc_session {
	struct crc_device *crc_dev;
	/* Locks */
	struct mutex call_lock;
	/* Complete iff waiting_count + scheduled_count == 0 */
	struct completion ioctl_comp;		// call_lock(w), dev_lock(w)
	/* Task stats */
	size_t waiting_count;			// dev_lock(rw)
	size_t scheduled_count;			// dev_lock(rw)
	/* Context */
	int ctx;				// dev_lock(rw)
	u32 poly;				// dev_lock(rw)
	u32 sum;				// dev_lock(rw)
};

struct crc_session * __must_check crc_session_alloc(struct crc_device *);
void crc_session_free(struct crc_session *);

/* crc_task */
struct crc_task {
	/* One task can be in one of the following: scheduled, waiting, free */
	struct list_head list;
	/* Session which this task belongs to */
	struct crc_session *session;
	/* This is a size of meaningful data in buffer */
	size_t data_count;
	/* Address of data in device's address space */
	dma_addr_t data_dma;
	u8 *data;
};

static __always_inline
void crc_task_attach(struct crc_session *sess, struct crc_task *task) {
	task->session = sess;
};

static __always_inline void crc_task_recycle(struct crc_task *task) {
	task->session = NULL;
	task->data_count = 0;
};

struct crc_command {
	__le32 addr;
	__le32 count_ctx;
} __packed;

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
	/* Contexts */
	DECLARE_BITMAP(contexts_map, CRCDEV_CTX_COUNT);		// dev_lock(rw)
	/* Tasks for this device */
	struct list_head free_tasks;		// dev_lock(rw)
	struct list_head waiting_tasks;		// dev_lock(rw)
	struct list_head scheduled_tasks;	// dev_lock(rw)
	/* BAR0 address */
	void __iomem *bar0;			// dev_lock(rw)
	/* Address of first cmd_block entry in dev address space */
	dma_addr_t cmd_block_dma;		// init
	struct crc_command *cmd_block;		// dev_lock(rw)
	/* Index in cmd_block of cmd next-to-be-processed by FETCH_DATA irq */
	size_t next_pos;
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
