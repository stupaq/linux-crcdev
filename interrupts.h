#ifndef INTERRUPTS_H_
#define INTERRUPTS_H_

#include <linux/interrupt.h>
#include <linux/irq.h>
#include "concepts.h"

#define CRCDEV_INTR_ALL	  (CRCDEV_INTR_FETCH_DATA | CRCDEV_INTR_FETCH_CMD_IDLE \
		| CRCDEV_INTR_FETCH_CMD_NONFULL)

irqreturn_t crc_irq_dispatcher(int, void *);

/* unsafe */
static __always_inline
void crc_irq_cmd_nonfull(struct crc_device *cdev, char enable) {
	u32 intr = ioread32(cdev->bar0 + CRCDEV_INTR_ENABLE);
	if (enable) intr |= CRCDEV_INTR_FETCH_CMD_NONFULL;
	else intr &= ~CRCDEV_INTR_FETCH_CMD_NONFULL;
	iowrite32(intr, cdev->bar0 + CRCDEV_INTR_ENABLE);
}

/* unsafe */
static __always_inline
void crc_irq_cmd_idle(struct crc_device *cdev, char enable) {
	u32 intr = ioread32(cdev->bar0 + CRCDEV_INTR_ENABLE);
	if (enable) intr |= CRCDEV_INTR_FETCH_CMD_IDLE;
	else intr &= ~CRCDEV_INTR_FETCH_CMD_IDLE;
	iowrite32(intr, cdev->bar0 + CRCDEV_INTR_ENABLE);
}

#endif  // INTERRUPTS_H_
