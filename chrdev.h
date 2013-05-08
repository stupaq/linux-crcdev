#ifndef CHRDEV_H_
#define CHRDEV_H_

#include <linux/device.h>
#include "concepts.h"

int __must_check crc_chrdev_init(void);

void crc_chrdev_exit(void);

dev_t crc_chrdev_getdev(struct crc_device *);

int __must_check crc_chrdev_add(struct pci_dev *, struct crc_device *);

void crc_chrdev_del(struct pci_dev *, struct crc_device *);

#endif  // CHRDEV_H_
