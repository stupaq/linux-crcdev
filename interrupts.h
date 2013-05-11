#ifndef INTERRUPTS_H_
#define INTERRUPTS_H_

#include <linux/interrupt.h>
#include <linux/irq.h>
#include "concepts.h"

irqreturn_t crc_irq_dispatcher(int, void *);

/* This enables needed interrupts ONLY (we do not use cmd_idle at all) */
static __always_inline void crc_irq_enable(struct crc_device *cdev) {
	iowrite32(CRCDEV_INTR_FETCH_DATA | CRCDEV_INTR_FETCH_CMD_NONFULL,
			(cdev)->bar0 + CRCDEV_INTR_ENABLE);
	mmiowb();
}

static __always_inline void crc_irq_disable_nonfull(struct crc_device *cdev) {
	iowrite32(CRCDEV_INTR_FETCH_DATA, cdev->bar0 + CRCDEV_INTR_ENABLE);
	mmiowb();
}

static __always_inline void crc_irq_fetch_data_ack(struct crc_device *cdev) {
	iowrite32(0, (cdev)->bar0 + CRCDEV_FETCH_DATA_INTR_ACK);
	mmiowb();
}

#endif  // INTERRUPTS_H_
