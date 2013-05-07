#ifndef PCI_H_
#define PCI_H_

#include <linux/pci.h>

#define CRCDEV_PCI_NAME	"crcdev"
#define CRCDEV_DMA_BITS	32

__must_check int crc_pci_init(void);

void crc_pci_exit(void);

int crc_pci_intline(struct pci_dev *);

#endif  /* PCI_H_ */
