#include "interrupts.h"
#include "concepts.h"
#include "crcdev.h"

/* CRITICAL SECTION */
static void crc_irq_handler_fetch_data(struct crc_device *cdev) {
	/* Handler must ACK this interrupt by itself */
	// FIXME
	crc_irq_fetch_data_ack(cdev);
}

/* CRITICAL SECTION */
static void crc_irq_handler_cmd_nonfull(struct crc_device *cdev) {
	// FIXME
	crc_irq_cmd_nonfull(cdev, 0);
}

/* CRITICAL SECTION */
static void crc_irq_handler_cmd_idle(struct crc_device *cdev) {
	// FIXME
	crc_irq_cmd_idle(cdev, 0);
}

irqreturn_t crc_irq_dispatcher(int irq, void *dev_id) {
	irqreturn_t rv;
	u32 intr;
	unsigned long flags;
	/* Interrupt handlers share crc_device reference with pci module, but
	 * this reference can only be destroyed after free_irq() */
	struct crc_device *cdev = (struct crc_device *) dev_id;
	/* BEGIN CRITICAL SECTION */
	spin_lock_irqsave(&cdev->dev_lock, flags);
	if (cdev->status & CRCDEV_STATUS_READY) {
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
	spin_unlock_irqrestore(&cdev->dev_lock, flags);
	/* END CRITICAL SECTION */
	return rv;
}
