/* The 2010 unit (8P0 035 193): an SH7770-family system-on-chip.
 *
 * No manual for the chip is public.  Every register written here is one
 * the stock update loader writes, with the loader's value and in the
 * loader's order; what the registers are is described, as far as it is
 * known, in docs/hardware-193.md.  Where the purpose of a write is not
 * known the comment says so: it is repeated because the real hardware
 * may need it, not because the emulator does. */
#include "rnse.h"

#define INTEVT 0xff000028

/* Interrupt controller. */
#define PINCTL 0xff802000 /* interrupt pins */
#define INT2PRI0 0xff804000
#define INT2MSKR 0xff804038   /* ones mask */
#define INT2MSKCR 0xff80403c  /* ones unmask */
#define INT2MSKR2 0xff804108
#define INT2MSKCR2 0xff80410c
#define MASK_TMU 0x00000002

/* Timer unit 0, channel 0. */
#define TSTR 0xffd80004
#define TCOR0 0xffd80008
#define TCNT0 0xffd8000c
#define TCR0 0xffd80010

/* Debug serial port. */
#define SCIF 0xffe40000
#define SCSMR (SCIF + 0x00)
#define SCSCR (SCIF + 0x08)
#define SCFTDR (SCIF + 0x0c)
#define SCFSR (SCIF + 0x10)
#define SCFCR (SCIF + 0x18)

/* Ports. */
#define PORT_A 0xffc40000
#define PORT_B 0xffc44000
#define WATCHDOG (PORT_B + 0x08) /* bit 0: strobe of the external watchdog */

/* Display unit. */
#define DU 0xfff80000
#define PLANE(n) (DU + 0x100 * (n))
#define PALETTE(n) (DU + 0x1000 * (n))
#define SURFACE 0x08110000  /* where the loader has its picture too */
#define STRIDE 832
/* The panel starts 16 columns and 8 lines into the surface with the
 * display timing below. */
#define LEFT 16
#define TOP 8

const int screen_width = 800, screen_height = 480;

static volatile u32 ticks;
static u32 strobe;

void interrupt(void)
{
    u32 code = REG32(INTEVT);
    if (code == 0x400) {
        /* Timer 0 underflow: every 10 ms. */
        REG16(TCR0) &= (u16)~0x0100;
        ticks += 10;
        /* The watchdog wants an edge on its strobe regularly; the stock
         * loader toggles it from this tick as well. */
        strobe ^= 1;
        REG32(WATCHDOG) = strobe;
    } else {
        unexpected(0, code, 0);
    }
}

u32 millis(void)
{
    return ticks;
}

void console_putc(char c)
{
    /* Wait for room in the transmit FIFO (TDFE), send, clear the flags. */
    while (!(REG16(SCFSR) & 0x0020))
        ;
    REG8(SCFTDR) = (u8)c;
    REG16(SCFSR) &= (u16)~0x0060;
}

void screen_palette(int index, u8 r, u8 g, u8 b)
{
    /* The plane uses the second of the four palettes. */
    REG32(PALETTE(2) + 4 * (index & 255)) = (u32)r << 16 | (u32)g << 8 | b;
}

void screen_pixel(int x, int y, u8 colour)
{
    if (x < 0 || y < 0 || x >= screen_width || y >= screen_height)
        return;
    /* Through the uncached window, so that the display unit sees it. */
    REG8((SURFACE | 0xa0000000) + (TOP + y) * STRIDE + LEFT + x) = colour;
}

void board_init(void)
{
    int i;

    /* Interrupt pins and controller: priorities zero, everything
     * masked.  The first eight writes are the loader's; their meaning
     * is not known in detail. */
    REG32(PINCTL + 0x00) = 0x00600000;
    REG32(PINCTL + 0x1c) = 0x00e80000;
    REG32(PINCTL + 0x10) = 0x00000000;
    REG32(PINCTL + 0x44) = 0xff000000;
    REG32(PINCTL + 0x48) = 0x80000000;
    REG32(0xff803000) = 0xa5000000;
    REG32(PINCTL + 0x64) = 0x04000000;
    REG32(PINCTL + 0x68) = 0x80000000;
    REG32(0xff800020) = 0xff902200;
    for (i = 0; i < 12; i++)
        REG32(INT2PRI0 + 4 * i) = 0;
    REG32(INT2MSKR) = 0xffffffff;
    REG32(INT2MSKR2) = 0xffffffff;

    /* Timers stopped. */
    REG8(TSTR) = 0;
    REG16(TCR0) = 0;

    /* Three writes of the loader's to a block of unknown purpose. */
    REG32(0xfe00802c) = 0;
    REG32(0xfe00803c) = 0;
    REG16(0xfe008060) = 1;

    /* Debug serial port: FIFOs reset, format 8N1, the loader's clock
     * settings (+0x34 and +0x30: source and divisor of the bit clock),
     * transmitter on. */
    REG16(SCFCR) = 0x0006;
    REG16(SCSCR) = 0x0002;
    REG16(SCSMR) = 0x0000;
    REG16(SCIF + 0x34) = 0x4000;
    REG16(SCIF + 0x30) = 0x0036;
    REG16(SCIF + 0x20) = 0x0000;
    REG16(SCFCR) = 0x0000;
    REG16(SCSCR) = 0x0022;

    /* Graphics engine: reset and released; not used here. */
    REG32(0xffe80000) = 0x80000000;
    REG32(0xffe80000) = 0x00000000;
    REG32(0xffe80060) = 0x0000000e;

    /* Ports: bits 3 and 4 of port A are outputs and driven high (what
     * they switch is not known; the picture may depend on them). */
    REG32(PORT_A + 0x20) = 0;
    REG32(PORT_A + 0x00) = 0;
    REG32(PORT_A + 0x04) = 0x18;
    REG32(PORT_B + 0x20) = 0;
    REG32(PORT_B + 0x00) = 0;
    REG32(PORT_B + 0x04) = 0;
    REG32(PORT_A + 0x08) = 0x18;
    REG32(WATCHDOG) = strobe = 1;

    /* Display unit, stopped while it is set up: 832 x 496 display
     * period in a 1056 x 525 frame, data enable 800 wide. */
    REG32(DU + 0x00) = 0x00100000;
    REG32(DU + 0x04) = 0x07000000;
    REG32(DU + 0x0c) = 0x0000cb3f; /* clear every status bit */
    REG32(DU + 0x10) = 0;          /* no interrupts from it */
    REG32(DU + 0x14) = 0;
    REG32(DU + 0x18) = 0x76543210; /* no plane shown yet */
    for (i = 0x80; i <= 0x9c; i += 4)
        REG32(DU + i) = 0;
    REG32(0xfff90000) = 0x00100008;
    REG32(DU + 0x40) = 0x0b5;
    REG32(DU + 0x44) = 0x3f5;
    REG32(DU + 0x48) = 0x018;
    REG32(DU + 0x4c) = 0x208;
    REG32(DU + 0x50) = 0x41f;
    REG32(DU + 0x54) = 0x03f;
    REG32(DU + 0x58) = 0x20c;
    REG32(DU + 0x5c) = 0x20a;
    REG32(DU + 0x78) = 0x0d7;
    REG32(DU + 0x7c) = 0x320;
    REG32(DU + 0x00) = 0x00100100; /* running */

    /* A block the loader sets up next: a key, then three registers.
     * Purpose not known. */
    REG32(0xfe800100) = 0x0000a55a;
    REG32(0xfe800108) = 0x00000000;
    REG32(0xfe800110) = 0x00000004;
    REG32(0xfe800108) = 0x00010000;
    REG32(0xfe800220) = 0xfffffffe;
    REG32(0xfe800108) = 0x00000000;

    /* A black picture before anything shows. */
    for (i = 0; i < STRIDE * 496; i += 4)
        REG32((SURFACE | 0xa0000000) + i) = 0;
    for (i = 0; i < 256; i++)
        screen_palette(i, 0, 0, 0);

    /* Plane 6: 8 bits per pixel through palette 2, no transparent
     * colour, the whole display period; the other planes off. */
    for (i = 1; i <= 8; i++)
        REG32(PLANE(i)) = 0;
    REG32(PLANE(6) + 0x00) = 0x00024100;
    REG32(PLANE(6) + 0x04) = STRIDE;
    REG32(PLANE(6) + 0x08) = 0xff;
    REG32(PLANE(6) + 0x10) = STRIDE;
    REG32(PLANE(6) + 0x14) = 496;
    REG32(PLANE(6) + 0x18) = 0;
    REG32(PLANE(6) + 0x1c) = 0;
    REG32(PLANE(6) + 0x20) = SURFACE;
    REG32(PLANE(6) + 0x24) = SURFACE;
    REG32(PLANE(6) + 0x28) = SURFACE;
    REG32(PLANE(6) + 0x30) = 0;
    REG32(PLANE(6) + 0x34) = 0;
    REG32(PLANE(6) + 0x38) = 0x3ff0;
    REG32(PLANE(6) + 0x3c) = 0x0fff;
    REG32(PLANE(6) + 0x40) = 0;
    REG32(PLANE(6) + 0x44) = 0;
    REG32(PLANE(6) + 0x48) = 0;
    REG32(PLANE(6) + 0x50) = 0x400;
    REG32(DU + 0x18) = 0x76d43210; /* plane 6 shown */
    REG32(DU + 0x14) = 0x000f0000;

    /* The tick: timer 0 at a quarter of the 25 MHz peripheral clock,
     * 62438 counts = 10 ms, interrupt level 7. */
    REG16(TCR0) = 0x0020;
    REG32(TCOR0) = 0xf3e5;
    REG32(TCNT0) = 0xf3e5;
    REG32(INT2PRI0) = 0x000e0000;
    REG32(INT2MSKCR) = MASK_TMU;
    REG8(TSTR) = 1;

    {
        extern void irq_enable(void);
        irq_enable();
    }
}
