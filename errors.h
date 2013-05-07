#ifndef CEXCEPT_H_
#define CEXCEPT_H_

#include <linux/kernel.h>
#include <linux/err.h>

#define	ERROR(rv)     (IS_ERR_VALUE(rv) ? rv : -EFAULT)
#define	CRCDEV_INFO   (KERN_INFO "crcdev: ")
#define	CRCDEV_WARN   (KERN_WARNING "crcdev: ")
#define	CRCDEV_ERR    (KERN_ERR "crcdev: ")

#endif  /* CEXCEPT_H_ */
