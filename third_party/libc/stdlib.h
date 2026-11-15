/* The little of a C library that third-party code in sdk/third_party
 * wants, for the freestanding SDK.  malloc() and free() are in
 * sdk/src/heap.c: a bump allocator over a block of RAM the program
 * names with heap_init(); free() gives nothing back. */
#ifndef RNSE_LIBC_STDLIB_H
#define RNSE_LIBC_STDLIB_H
#include <stddef.h>
void *malloc(unsigned long n); /* as sdk/include/rnse.h has it */
void free(void *p);
#endif
