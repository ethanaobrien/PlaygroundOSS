/* Target configuration for the bundled Independent JPEG Group runtime. */
#ifndef SIF_LIBJPEG_JCONFIG_H
#define SIF_LIBJPEG_JCONFIG_H

#define HAVE_PROTOTYPES 1
#define HAVE_UNSIGNED_CHAR 1
#define HAVE_UNSIGNED_SHORT 1
#define HAVE_STDDEF_H 1
#define HAVE_STDLIB_H 1
#define HAVE_LOCALE_H 1

/*
 * The shipped Android runtime uses the compact IJG boolean ABI.  Constructor
 * and codec field accesses across all 46 objects independently verify this
 * one-byte type; the default enum layout does not match those objects.
 */
typedef unsigned char boolean;
#define HAVE_BOOLEAN 1
#define FALSE 0
#define TRUE 1

#ifdef JPEG_INTERNALS
#define INLINE __inline__
#endif

#endif
