#ifndef DMABUFEXP_H_
#define DMABUFEXP_H_

#include <linux/kernel.h>

#define DMABUFEXP_MAGIC 'D'
#define DMABUFEXP_IO_EXPORT _IOWR(DMABUFEXP_MAGIC, 0, int)
#define DMABUFEXP_IO_IMPORT _IOR(DMABUFEXP_MAGIC, 0, int)

#define DMABUFEXP_BUF_SIZE 4096

#endif
