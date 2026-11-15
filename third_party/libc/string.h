#ifndef RNSE_LIBC_STRING_H
#define RNSE_LIBC_STRING_H
#include <stddef.h>
/* In sdk/src/common.c. */
void *memset(void *d, int c, unsigned long n);
void *memcpy(void *d, const void *s, unsigned long n);
#endif
