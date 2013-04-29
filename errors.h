#ifndef CEXCEPT_H_
#define CEXCEPT_H_

#define ERROR(rv) (rv < 0 ? rv : -EFAULT)

#endif  /* CEXCEPT_H_ */
