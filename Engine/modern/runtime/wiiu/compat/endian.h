#ifndef PLAYGROUND_WIIU_COMPAT_ENDIAN_H
#define PLAYGROUND_WIIU_COMPAT_ENDIAN_H

/* glibc-compatible spellings required by Tremolo on big-endian Espresso. */
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
