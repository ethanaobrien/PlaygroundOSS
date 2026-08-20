#ifndef PLAYGROUND_SWITCH_COMPAT_ENDIAN_H
#define PLAYGROUND_SWITCH_COMPAT_ENDIAN_H

// Tremolo uses the glibc spelling. Newlib exposes the same constants from
// sys/endian.h under underscored names.
#include <sys/endian.h>

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN _LITTLE_ENDIAN
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN _BIG_ENDIAN
#endif
#ifndef BYTE_ORDER
#define BYTE_ORDER _BYTE_ORDER
#endif

#endif
