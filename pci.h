#ifndef PCI_H_
#define PCI_H_

#define CRCDEV_PCI_NAME	"crcdev"
#define CRCDEV_BAR0	0
#define CRCDEV_DMABITS	32

__must_check int crc_pci_init(void);

void crc_pci_exit(void);

int crc_pci_intline(struct pci_dev *);

#endif  /* PCI_H_ */
