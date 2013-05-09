#ifndef CEXCEPT_H_
#define CEXCEPT_H_

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/bug.h>

// TODO remove this
#define	ERROR(rv)     (IS_ERR_VALUE(rv) ? rv : -EFAULT)

#define crc_error_hot_unplug() printk(KERN_WARNING \
		"crcdev: device removed while there was pending syscall")

#endif  /* CEXCEPT_H_ */
