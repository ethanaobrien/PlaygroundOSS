#ifndef PLAYGROUND_WIIU_COMPAT_SYS_UIO_H
#define PLAYGROUND_WIIU_COMPAT_SYS_UIO_H

#include <stddef.h>

/* msgpack 0.5 uses iovec only as an in-memory span descriptor. */
struct iovec {
  void *iov_base;
  size_t iov_len;
};

#endif
