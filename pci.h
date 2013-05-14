#ifndef PCI_H_
#define PCI_H_

#include <linux/pci.h>
#include "crcdev.h"

#define CRCDEV_PCI_NAME	"crcdev"
#define CRCDEV_DMA_BITS	32

static void __always_inline crc_pci_iomb(void __iomem* bar0) {
	mmiowb();
	ioread32(bar0 + CRCDEV_STATUS);
}

int __must_check crc_pci_init(void);

void crc_pci_exit(void);

void crc_reset_device(void __iomem*);

#endif  /* PCI_H_ */
