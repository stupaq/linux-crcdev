#ifndef PCI_H_
#define PCI_H_

#define CRCDEV_PCI_NAME "crcdev"

__must_check int crcdev_pci_init(void);

void crcdev_pci_exit(void);

#endif  /* PCI_H_ */
