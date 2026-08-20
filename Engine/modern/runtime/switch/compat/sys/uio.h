#ifndef PLAYGROUND_SWITCH_COMPAT_SYS_UIO_H
#define PLAYGROUND_SWITCH_COMPAT_SYS_UIO_H

// Newlib/libnx keeps iovec in the internal POSIX compatibility header while
// the bundled msgpack release includes the conventional sys/uio.h spelling.
#include <sys/_iovec.h>

#endif
