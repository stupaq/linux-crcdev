#include "interrupts.h"
#include "concepts.h"
#include "crcdev.h"
#include "monitors.h"

MODULE_LICENSE("GPL");

/* Hardware abstraction layer */
#define	cdev_next_cmd_idx(cdev, idx) (((idx) + 1) % (CRCDEV_COMMANDS_LENGTH))
#define	cdev_write_pos(cdev) ioread32((cdev)->bar0 + CRCDEV_FETCH_CMD_WRITE_POS)
#define	cdev_is_cmd_full(cdev)	({ \
		BUILD_BUG_ON(CRCDEV_COMMANDS_LENGTH <= CRCDEV_BUFFERS_COUNT); \
		0; })
#define	cdev_pending_done(cdev)	do { (cdev)->next_pos = \
	cdev_next_cmd_idx(cdev, (cdev)->next_pos); } while(0)

static __always_inline int cdev_is_pending(struct crc_device *cdev) {
	u32 read_pos;
	u32 status;
	/* Do not reorder these under any circumstances */
	read_pos = ioread32(cdev->bar0 + CRCDEV_FETCH_CMD_READ_POS);
	status = ioread32(cdev->bar0 + CRCDEV_STATUS);
	if (read_pos == cdev->next_pos)
		return false;
	if (status & CRCDEV_STATUS_FETCH_DATA)
		return cdev_next_cmd_idx(cdev, cdev->next_pos) != read_pos;
	return true;
}

static __always_inline void cdev_put_command(struct crc_task *task) {
	struct crc_device *cdev = task->session->crc_dev;
	size_t idx = cdev_write_pos(cdev);
	struct crc_command *cmd = cdev->cmd_block + idx;
	size_t ctx = task->session->ctx;
	cmd->count_ctx = cpu_to_le32((task->data_count & CRCDEV_CMD_COUNT_MASK)
			| ((ctx & CRCDEV_CMD_CTX_MASK) <<
				CRCDEV_CMD_CTX_SHIFT));
	cmd->addr = cpu_to_le32(task->data_dma);
	my_debug("irq: cmd: idx %u ctx %u count %u addr %x",
			idx,
			le32_to_cpu(cmd->count_ctx) >> CRCDEV_CMD_CTX_SHIFT,
			le32_to_cpu(cmd->count_ctx) & CRCDEV_CMD_CTX_MASK,
			le32_to_cpu(cmd->addr));
	idx = cdev_next_cmd_idx(cdev, idx);
	iowrite32(idx, cdev->bar0 + CRCDEV_FETCH_CMD_WRITE_POS);
	mmiowb();
}

static __always_inline void cdev_get_context(struct crc_session *sess) {
	BUG_ON(sess->ctx < 0 || CRCDEV_CTX_COUNT <= sess->ctx);
	my_debug("irq: get: ctx %u poly %x sum %x", sess->ctx, sess->poly,
			sess->sum);
	sess->poly = ioread32(sess->crc_dev->bar0 + CRCDEV_CRC_POLY(sess->ctx));
	sess->sum = ioread32(sess->crc_dev->bar0 + CRCDEV_CRC_SUM(sess->ctx));
}

static __always_inline void cdev_put_context(struct crc_session *sess) {
	BUG_ON(sess->ctx < 0 || CRCDEV_CTX_COUNT <= sess->ctx);
	iowrite32(sess->poly, sess->crc_dev->bar0 + CRCDEV_CRC_POLY(sess->ctx));
	iowrite32(sess->sum, sess->crc_dev->bar0 + CRCDEV_CRC_SUM(sess->ctx));
	mmiowb();
	my_debug("irq: put: ctx %u poly %x sum %x", sess->ctx, sess->poly,
			sess->sum);
}

/* Device status (direct) */
static __always_inline void cdev_report_status(struct crc_device *cdev) {
	my_debug("dev %u: enable %u status %u intr %u intr_e %u\n"
			"               next %u read %u write %u length %u",
			cdev->minor,
			ioread32(cdev->bar0 + CRCDEV_ENABLE),
			ioread32(cdev->bar0 + CRCDEV_STATUS),
			ioread32(cdev->bar0 + CRCDEV_INTR),
			ioread32(cdev->bar0 + CRCDEV_INTR_ENABLE),
			cdev->next_pos,
			ioread32(cdev->bar0 + CRCDEV_FETCH_CMD_READ_POS),
			ioread32(cdev->bar0 + CRCDEV_FETCH_CMD_WRITE_POS),
			ioread32(cdev->bar0 + CRCDEV_FETCH_CMD_SIZE));
}

/* CRITICAL (interrupt) */
static void crc_irq_handler_fetch_data(struct crc_device *cdev) {
	struct crc_task *task;
	struct crc_session *sess;
	/* This interrupt must be ACKed before we start processing
	 * pending tasks, do not reorder these */
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
		crc_task_recycle(task);
		list_add(&task->list, &cdev->free_tasks);
		mon_session_free_task(sess);
		cdev_pending_done(cdev);
	}
	/* Enable nonfull */
	crc_irq_enable(cdev);
}

/* CRITICAL (interrupt) */
static void crc_irq_handler_cmd_nonfull(struct crc_device *cdev) {
	struct crc_task *task;
	struct crc_session *sess;
	/* Interrupt priorities: FETCH_DATA served */
	while (!list_empty(&cdev->waiting_tasks)) {
		if (cdev_is_cmd_full(cdev))
			goto cmd_block_full;
		task = list_first_entry(&cdev->waiting_tasks, struct crc_task,
				list);
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
		list_add_tail(&task->list, &cdev->scheduled_tasks);
		sess->scheduled_count++;
		cdev_put_command(task);
	}
	/* We've run out of tasks */
	crc_irq_disable_nonfull(cdev);
	return;
no_free_context:
	my_debug("irq: no free context");
	crc_irq_disable_nonfull(cdev);
	return;
cmd_block_full:
	my_debug("irq: cmd block full ");
	crc_irq_disable_nonfull(cdev);
	return;
}

/* CRITICAL (interrupt) */
static void crc_irq_handler_cmd_idle(struct crc_device *cdev) {
	/* Interrupt priorities: FETCH_DATA and CMD_NONFULL served */
	printk(KERN_WARNING "crcdev: irq: cmd_idle fired");
	/* Enable only necessary interrupts (not this one) */
	crc_irq_enable(cdev);
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
		cdev_report_status(cdev);
		/* Check if it was our device and which interrupt fired */
		intr = ioread32(cdev->bar0 + CRCDEV_INTR);
		intr &= ioread32(cdev->bar0 + CRCDEV_INTR_ENABLE);
		/* Priorities here are important */
		if (intr & CRCDEV_INTR_FETCH_DATA) {
			my_debug("irq: fetch_data");
			crc_irq_handler_fetch_data(cdev);
			rv = IRQ_HANDLED;
		} else if (intr & CRCDEV_INTR_FETCH_CMD_NONFULL) {
			my_debug("irq: cmd_nonfull");
			crc_irq_handler_cmd_nonfull(cdev);
			rv = IRQ_HANDLED;
		} else if (intr & CRCDEV_INTR_FETCH_CMD_IDLE) {
			my_debug("irq: cmd_idle");
			crc_irq_handler_cmd_idle(cdev);
			rv = IRQ_HANDLED;
		} else {
			/* This is for sure */
			rv = IRQ_NONE;
		}
	} else {
		/* We are not sure this time, if it was our device then it's OK,
		 * otherwise proper device will raise this again */
		rv = IRQ_HANDLED;
	}
	/* If device is not ready then crc_remove has been called and interrupts
	 * are already disabled, this interrupt won't be raised again if it came
	 * from our device */
	mon_device_unlock(cdev, flags);
	/* EXIT (interrupt) */
	my_debug("irq: exit");
	return rv;
}
