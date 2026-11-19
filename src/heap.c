/* malloc() and free() for code that wants them (third_party): a
 * bump allocator over a block of RAM the program hands to heap_init.
 * free() gives nothing back; a program that is done with everything
 * calls heap_init again.  Blocks are aligned to 32 bytes, the SH-4's
 * cache line. */
#include "rnse.h"

static u8 *heap_next, *heap_end;

void heap_init(void *base, u32 size)
{
    heap_next = (u8 *)(((u32)base + 31) & ~31u);
    heap_end = (u8 *)base + size;
}

u32 heap_left(void)
{
    return heap_next < heap_end ? (u32)(heap_end - heap_next) : 0;
}

void *malloc(unsigned long n)
{
    u8 *p = heap_next;
    n = (n + 31) & ~31ul;
    if (!p || n > heap_left())
        return 0;
    heap_next += n;
    return p;
}

void free(void *p)
{
    (void)p;
}
