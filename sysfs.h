#ifndef SYSFS_H_
#define SYSFS_H_

#include "concepts.h"

#define CRCDEV_CLASS_NAME "crcdev"

int __must_check crc_sysfs_init(void);

void crc_sysfs_exit(void);

int __must_check crc_sysfs_add(struct pci_dev *, struct crc_device *);

void crc_sysfs_del(struct pci_dev *, struct crc_device *);

#endif  // SYSFS_H_
