#ifndef PCI_H_
#define PCI_H_

#include <linux/pci.h>

#define CRCDEV_PCI_NAME	"crcdev"
#define CRCDEV_DMA_BITS	32

int __must_check crc_pci_init(void);

void crc_pci_exit(void);

void crc_reset_device(void __iomem*);

#endif  /* PCI_H_ */
