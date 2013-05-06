#include "interrupts.h"
#include "concepts.h"
#include "crcdev.h"

static void crc_irq_handler_fetch_data(struct crc_device *cdev) {
	printk(KERN_INFO "crcdev: irq: fetch_data");
	// FIXME
}

static void crc_irq_handler_cmd_nonfull(struct crc_device *cdev) {
	printk(KERN_INFO "crcdev: irq: cmd_nonfull");
	// FIXME
	crc_irq_cmd_nonfull(cdev, 0);
}

static void crc_irq_handler_cmd_idle(struct crc_device *cdev) {
	printk(KERN_INFO "crcdev: irq: cmd_idle");
	// FIXME
	crc_irq_cmd_idle(cdev, 0);
}

// FIXME check some device-ready flag, this is asynchronous from device remove!
irqreturn_t crc_irq_dispatcher(int irq, void *dev_id) {
	struct crc_device *cdev = (struct crc_device *) dev_id;
	/* Check if it was our device and which interrupt fired */
	u32 intr = ioread32(cdev->bar0 + CRCDEV_INTR);
	/* Priorities here are important */
	if (intr & CRCDEV_INTR_FETCH_DATA) {
		/* Handler must ACK this interrupt */
		crc_irq_handler_fetch_data(cdev);
		return IRQ_HANDLED;
	} else if (intr & CRCDEV_INTR_FETCH_CMD_NONFULL) {
		crc_irq_handler_cmd_nonfull(cdev);
		return IRQ_HANDLED;
	} else if (intr & CRCDEV_INTR_FETCH_CMD_IDLE) {
		crc_irq_handler_cmd_idle(cdev);
		return IRQ_HANDLED;
	}
	/* It wasn't our device which raised this interrupt */
	return IRQ_NONE;
}
