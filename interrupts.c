#include "interrupts.h"
#include "concepts.h"
#include "crcdev.h"
#include "monitors.h"

MODULE_LICENSE("GPL");

/* Hardware abstraction layer */
#define circular_next(idx, len)	(((idx) + 1) % (len))

#define cdev_read_pos(cdev) ioread32((cdev)->bar0 + CRCDEV_FETCH_CMD_READ_POS)
#define cdev_write_pos(cdev) ioread32((cdev)->bar0 + CRCDEV_FETCH_CMD_WRITE_POS)
#define cdev_set_write_pos(cdev, idx) \
	iowrite32(idx, (cdev)->bar0 + CRCDEV_FETCH_CMD_WRITE_POS);

#define cdev_is_pending(cdev)	((cdev)->next_pos != cdev_read_pos(cdev))
#define cdev_is_cmd_full(cdev)	({ \
		BUILD_BUG_ON(CRCDEV_COMMANDS_LENGTH <= CRCDEV_BUFFERS_COUNT); \
		0; })
#define cdev_pending_done(cdev)	{ (cdev)->next_pos = \
	circular_next((cdev)->next_pos, CRCDEV_COMMANDS_LENGTH); }

static __always_inline void cdev_put_command(struct crc_task *task) {
	struct crc_device *cdev = task->session->crc_dev;
	size_t idx = cdev_write_pos(cdev);
	struct crc_command *cmd = cdev->cmd_block + idx;
	size_t ctx = task->session->ctx;
	cmd->count_ctx = cpu_to_le32((task->data_count & CRCDEV_CMD_COUNT_MASK)
			| ((ctx & CRCDEV_CMD_CTX_MASK) <<
				CRCDEV_CMD_CTX_SHIFT));
	cmd->addr = cpu_to_le32(task->data_dma);
	idx = circular_next(idx, CRCDEV_COMMANDS_LENGTH);
	cdev_set_write_pos(cdev, idx);
}

static __always_inline void cdev_get_context(struct crc_session *sess) {
	BUG_ON(sess->ctx < 0 || CRCDEV_CTX_COUNT <= sess->ctx);
	sess->poly = ioread32(sess->crc_dev->bar0 + CRCDEV_CRC_POLY(sess->ctx));
	sess->sum = ioread32(sess->crc_dev->bar0 + CRCDEV_CRC_SUM(sess->ctx));
}

static __always_inline void cdev_put_context(struct crc_session *sess) {
	BUG_ON(sess->ctx < 0 || CRCDEV_CTX_COUNT <= sess->ctx);
	iowrite32(sess->poly, sess->crc_dev->bar0 + CRCDEV_CRC_POLY(sess->ctx));
	iowrite32(sess->sum, sess->crc_dev->bar0 + CRCDEV_CRC_SUM(sess->ctx));
}

static __always_inline void cdev_report_status(struct crc_device *cdev) {
	printk(KERN_INFO "crcdev: enable: %u status: %u intr: %u intr_e: %u\n"
			 "        next: %u read: %u write %u length: %u",
			ioread32(cdev->bar0 + CRCDEV_ENABLE),
			ioread32(cdev->bar0 + CRCDEV_STATUS),
			ioread32(cdev->bar0 + CRCDEV_INTR),
			ioread32(cdev->bar0 + CRCDEV_INTR_ENABLE),
			cdev->next_pos,
			ioread32(cdev->bar0 + CRCDEV_FETCH_CMD_READ_POS),
			ioread32(cdev->bar0 + CRCDEV_FETCH_CMD_WRITE_POS),
			ioread32(cdev->bar0 + CRCDEV_FETCH_CMD_SIZE));
}

/* Handlers can inherit critical section from each other */
/* CRITICAL (interrupt) */
static void crc_irq_handler_fetch_data(struct crc_device *cdev) {
	struct crc_task *task;
	struct crc_session *sess;
	/* Handler must ACK this interrupt by itself, we do this immediately as
	 * we will handle all pending tasks in loop */
	crc_irq_fetch_data_ack(cdev);
	while (cdev_is_pending(cdev)) {
		task = list_first_entry(&cdev->scheduled_tasks, struct crc_task,
				list);
		sess = task->session;
		sess->scheduled_count--;
		if (0 == sess->scheduled_count) {
			/* Sync context to session */
			cdev_get_context(sess);
			/* Free context */
			clear_bit(sess->ctx, cdev->contexts_map);
			sess->ctx = CRCDEV_SESSION_NOCTX;
			if (0 == sess->waiting_count) {
				complete_all(&sess->ioctl_comp);
			}
		}
		list_del(&task->list);
		list_add(&task->list, &cdev->free_tasks);
		mon_session_free_task(sess);
		cdev_pending_done(cdev);
	}
	/* Enable all */
	crc_irq_enable_all(cdev);
}

/* CRITICAL (interrupt) */
static void crc_irq_handler_cmd_nonfull(struct crc_device *cdev) {
	struct crc_task *task;
	struct crc_session *sess;
	/* Interrupt priorities: FETCH_DATA served */
	if (cdev_is_pending(cdev) || cdev_is_cmd_full(cdev)) {
		printk(KERN_INFO "crcdev: irq: fetch data (lost) ");
		crc_irq_handler_fetch_data(cdev);
		return;
	}
	if (list_empty(&cdev->waiting_tasks))
		goto no_waiting_task;
	task = list_first_entry(&cdev->waiting_tasks, struct crc_task, list);
	sess = task->session;
	if (CRCDEV_SESSION_NOCTX == sess->ctx) {
		/* Find and allocate context */
		int ctx = find_first_zero_bit(cdev->contexts_map,
				CRCDEV_CTX_COUNT);
		if (ctx < 0 || CRCDEV_CTX_COUNT <= ctx)
			goto no_free_context;
		set_bit(ctx, cdev->contexts_map);
		/* Sync device with session */
		sess->ctx = ctx;
		cdev_put_context(sess);
	}
	BUG_ON(sess->ctx < 0 || CRCDEV_CTX_COUNT <= sess->ctx);
	/* Session has a context, schedule task */
	list_del(&task->list);
	sess->waiting_count--;
	list_add(&task->list, &cdev->scheduled_tasks);
	sess->scheduled_count++;
	cdev_put_command(task);
	/* Enable all */
	crc_irq_enable_all(cdev);
	return;
no_free_context:
	printk(KERN_INFO "crcdev: irq: no free context");
	crc_irq_cmd_nonfull(cdev, 0);
	crc_irq_cmd_idle(cdev, 1);
	return;
no_waiting_task:
	printk(KERN_INFO "crcdev: irq: no waiting task");
	crc_irq_cmd_nonfull(cdev, 0);
	return;
}

/* CRITICAL (interrupt) */
static void crc_irq_handler_cmd_idle(struct crc_device *cdev) {
	/* Interrupt priorities: FETCH_DATA and CMD_NONFULL served */
	if (cdev_is_pending(cdev)) {
		printk(KERN_INFO "crcdev: irq: fetch data (lost) ");
		crc_irq_handler_fetch_data(cdev);
		return;
	}
	crc_irq_cmd_idle(cdev, 0);
}

irqreturn_t crc_irq_dispatcher(int irq, void *dev_id) {
	irqreturn_t rv;
	u32 intr;
	unsigned long flags;
	/* Interrupt handlers share crc_device reference with pci module, but
	 * this reference can only be destroyed after free_irq() */
	struct crc_device *cdev = (struct crc_device *) dev_id;
	/* ENTER (interrupt) */
	mon_device_lock(cdev, flags);
	if (test_bit(CRCDEV_STATUS_READY, &cdev->status)) {
		// TODO remove debug printks
		cdev_report_status(cdev);
		/* Check if it was our device and which interrupt fired */
		intr = ioread32(cdev->bar0 + CRCDEV_INTR);
		intr &= ioread32(cdev->bar0 + CRCDEV_INTR_ENABLE);
		/* Priorities here are important */
		if (intr & CRCDEV_INTR_FETCH_DATA) {
			printk(KERN_INFO "crcdev: irq: fetch_data");
			crc_irq_handler_fetch_data(cdev);
			rv = IRQ_HANDLED;
		} else if (intr & CRCDEV_INTR_FETCH_CMD_NONFULL) {
			printk(KERN_INFO "crcdev: irq: cmd_nonfull");
			crc_irq_handler_cmd_nonfull(cdev);
			rv = IRQ_HANDLED;
		} else if (intr & CRCDEV_INTR_FETCH_CMD_IDLE) {
			printk(KERN_INFO "crcdev: irq: cmd_idle");
			crc_irq_handler_cmd_idle(cdev);
			rv = IRQ_HANDLED;
		} else {
			/* This is for sure */
			rv = IRQ_NONE;
		}
	} else {
		/* We are not sure this time, if it was our device then it's OK,
		 * otherwise proper device will raise this again */
		rv = IRQ_HANDLED; // FIXME
	}
	/* If device is not ready then crc_remove has been called and interrupts
	 * are already disabled, this interrupt won't be raised again if it came
	 * from our device */
	mon_device_unlock(cdev, flags);
	/* EXIT (interrupt) */
	printk(KERN_INFO "crcdev: irq: exit");
	return rv;
}
