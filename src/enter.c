/* Handing the processor to another image in RAM: what linuxboot does
 * for the kernel and doomboot for the game, each too big for the
 * loader's place.  On the 192 and the 193 (image_enter_193 below).
 *
 * Interrupts masked; the timers, every DMA channel, the on-chip and the
 * companion chip's interrupt sources stopped, as loader_return stops
 * them, so that nothing goes on writing memory or asking for interrupts
 * under the new image; the operand cache written back and invalidated
 * over [start, end) (the image, and the room for its bss: the new image
 * clears that through the cache), the watchdog strobed meanwhile; and
 * the last step run uncached, since it changes the cache's control: the
 * instruction cache invalidated (CCR.ICI, eight instructions before the
 * branch as the SH-4 manual wants; the code of whatever ran there
 * before may still be in it) and a jump to the entry.  Does not return. */
#include "rnse.h"

void irq_disable(void);
void image_jump(u32 entry);

/* The 193: its watchdog's strobe is bit 0 of 0xffc44008, the rest of
 * that port's lines left as they are (src/hw193.c); its interrupt
 * controller has the SH7780's INTC2 layout at 0xff804000, every source
 * masked through +0x38 and +0x108 and its priorities zeroed; timer unit
 * 0 (the tick) and unit 1 (the stock software's CAN timer) stopped.
 * The stock software's other DMA is not known to run under a kit
 * program (the kit's own SD reads are programmed I/O). */
#define WATCHDOG_193 0xffc44008

static void strobe_193(void)
{
    REG32(WATCHDOG_193) ^= 1;
}

static int image_enter_193(u32 entry, u32 start, u32 end)
{
    u32 a;
    int i;

    irq_disable();
    REG8(0xffd80004) = 0;             /* timer unit 0 stopped */
    REG16(0xffd80010) = 0;
    REG32(0xffd81004) = 0;            /* timer unit 1 (CAN's) stopped */
    REG32(0xff804038) = 0xffffffff;   /* every on-chip source masked */
    REG32(0xff804108) = 0xffffffff;
    for (i = 0; i < 12; i++)
        REG32(0xff804000 + 4 * i) = 0;
    for (a = start & ~31u; a < end; a += 32) {
        __asm__ volatile ("ocbp @%0" : : "r"(a) : "memory");
        if (!(a & 0xffff))
            strobe_193();
    }
    strobe_193();
    ((void (*)(u32))(((u32)image_jump & 0x1fffffff) | 0xa0000000))(entry);
    return -1; /* not reached */
}

int image_enter(u32 entry, u32 start, u32 end)
{
    u32 a;
    int i;

    if (screen_width != 400)
        return image_enter_193(entry, start, end);
    irq_disable();
    REG8(0xffd80004) = 0;
    REG16(0xffd80010) = 0;
    for (i = 0; i < 8; i++)
        REG32(0xffa0000c + (i < 4 ? i : i + 1) * 0x10) = 0;
    REG32(0xffa00040) = 0;
    REG16(0xffd00004) = 0;
    REG16(0xffd00008) = 0;
    REG16(0xffd0000c) = 0;
    /* The companion chip: levels to 0, enables off. */
    for (i = 0; i <= 0x14; i += 2)
        REG16(0xa7b00000 + i) = 0;
    REG16(0xa7b0001e) = 0;
    /* (With an edge on the watchdog's strobe every 64 KiB: a purge of
     * all of RAM unstrobed outlasted the watchdog's 105 ms on a unit,
     * rnse-linux's docs/linux.md, and the time an ocbp takes there is not known.) */
    for (a = start & ~31u; a < end; a += 32) {
        __asm__ volatile ("ocbp @%0" : : "r"(a) : "memory");
        if (!(a & 0xffff))
            REG8(0xb2300001) ^= 1;
    }
    REG8(0xb2300001) ^= 1; /* the watchdog, once more */
    ((void (*)(u32))(((u32)image_jump & 0x1fffffff) | 0xa0000000))(entry);
    for (;;)
        ;
}

/* r4 = entry. */
__asm__(
    "    .text\n"
    "    .align 2\n"
    "    .global image_jump\n"
    "image_jump:\n"
    "    mov.l 1f, r1\n"
    "    mov.l @r1, r0\n"
    "    mov.l 2f, r2\n"
    "    or r2, r0\n"
    "    mov.l r0, @r1\n"
    "    nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n"
    "    jmp @r4\n"
    "    nop\n"
    "    .align 2\n"
    "1:  .long 0xff00001c\n"
    "2:  .long 0x00000800\n");
