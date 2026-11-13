/* The older unit (8P0 035 192): an SH7750R, a companion chip with the
 * serial ports and an interrupt controller on chip select 1, byte-wide
 * ports on chip select 4 and a Fujitsu Coral graphics controller on
 * chip select 6.
 *
 * Every register written here is one the stock update loader writes,
 * with the loader's value and in the loader's order; docs/hardware.md
 * says what is known about them.  The loader never sets the direction
 * of the ports it drives, so the boot ROM has done that before it
 * starts; a program in the loader's place inherits the same state. */
#include "rnse.h"

#define INTEVT 0xff000028

/* On-chip interrupt controller and timer unit. */
#define ICR 0xffd00000
#define IPRA 0xffd00004
#define IPRB 0xffd00008
#define IPRC 0xffd0000c
#define TOCR 0xffd80000
#define TSTR 0xffd80004
#define TCOR0 0xffd80008
#define TCNT0 0xffd8000c
#define TCR0 0xffd80010

/* Companion chip, through the uncached window. */
#define COMPANION_INTC 0xa7b00000
#define UART5 0xa7a50000 /* debug serial port */

/* Ports. */
#define PORT3_DATA 0xb2300001 /* bit 0: watchdog strobe */
#define PORT8_DATA 0xb2800001
#define LATCHES 0xb2d00000

/* Coral: host interface, display controller, video memory. */
#define CORAL_HOST 0xb9fc0000
#define CORAL_DISP 0xb9fd0000
#define VRAM 0xb8000000
#define SURFACE 0x00110000 /* offset in video memory, as the loader's */
#define STRIDE 448

const int screen_width = 400, screen_height = 235;

static volatile u32 ticks;

void interrupt(void)
{
    u32 code = REG32(INTEVT);
    if (code == 0x400) {
        /* Timer 0 underflow: every 10 ms. */
        REG16(TCR0) &= (u16)~0x0100;
        ticks += 10;
        /* The watchdog wants an edge on its strobe regularly. */
        REG8(PORT3_DATA) ^= 1;
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
    /* Status bit 9: transmit FIFO full. */
    while (REG16(UART5 + 0x08) & 0x0200)
        ;
    REG16(UART5 + 0x06) = (u8)c;
}

void screen_palette(int index, u8 r, u8 g, u8 b)
{
    /* Six bits a colour, in the upper bits of each byte. */
    REG32(CORAL_DISP + 0x800 + 4 * (index & 255)) = (u32)(r & 0xfc) << 16 | (u32)(g & 0xfc) << 8 | (b & 0xfc);
}

void screen_pixel(int x, int y, u8 colour)
{
    if (x < 0 || y < 0 || x >= screen_width || y >= screen_height)
        return;
    /* The chip is little-endian and the bus is not: pixel N of the
     * surface is the byte at address N ^ 3. */
    REG8(VRAM + SURFACE + ((y * STRIDE + x) ^ 3)) = colour;
}

void board_init(void)
{
    int i;

    /* Interrupt controllers: nothing enabled.  Timers stopped. */
    REG16(IPRA) = 0;
    REG16(IPRB) = 0;
    REG16(IPRC) = 0;
    REG16(ICR) = 0;
    REG8(TOCR) = 0;
    REG8(TSTR) = 0;
    REG16(TCR0) = 0;
    for (i = 0; i <= 0x14; i += 2)
        REG16(COMPANION_INTC + i) = 0;
    REG16(COMPANION_INTC + 0x1e) = 0x0040;

    /* Debug serial port: mode, bit rate, FIFO control, transmitter on. */
    REG16(UART5 + 0x00) = 0x0001;
    REG16(UART5 + 0x02) = 0x005f;
    REG16(UART5 + 0x0c) = 0x0000;
    REG16(UART5 + 0x04) = 0x0020;

    /* Output latches and port 8 as the loader sets them; which lines
     * these are is mostly unknown (docs/hardware.md, "Ports"). */
    REG8(LATCHES + 0) = 0x1f;
    for (i = 2; i <= 6; i++)
        REG8(LATCHES + i) = 0x1f;
    REG8(PORT8_DATA) = 0x00;
    REG8(PORT8_DATA) = 0x20;
    REG8(PORT8_DATA) = 0x60;
    REG32(0xb6000004) = 0x00000010;

    /* Coral.  The first three writes go to its host interface (a key,
     * then two set-up bytes), with the watchdog strobe raised around
     * them as the loader has it. */
    REG32(CORAL_HOST + 0xfffc) = 0x41d7f053;
    REG8(CORAL_HOST + 0x07) = 0x02;
    REG8(CORAL_HOST + 0x27) = 0x1f;
    REG8(PORT3_DATA) = 0x01;
    REG32(0xb6000004) = 0x00000010;
    REG8(CORAL_HOST + 0x2f) = 0x01;
    /* Display timing: 400 of 508 pixels a line, 235 of 263 lines. */
    REG16(CORAL_DISP + 0x02) = 0x1800;
    REG16(CORAL_DISP + 0x04) = 0x01fb;
    REG16(CORAL_DISP + 0x0a) = 0x018f;
    REG16(CORAL_DISP + 0x08) = 0x018f;
    REG16(CORAL_DISP + 0x0e) = 0x01b4;
    REG8(CORAL_DISP + 0x0d) = 0x24;
    REG8(CORAL_DISP + 0x0c) = 0x02;
    REG16(CORAL_DISP + 0x10) = 0x0106;
    REG16(CORAL_DISP + 0x14) = 0x00ea;
    REG16(CORAL_DISP + 0x16) = 0x00f2;
    /* Both palettes black. */
    for (i = 0x400; i < 0xc00; i += 4)
        REG32(CORAL_DISP + i) = 0;
    REG16(CORAL_DISP + 0x00) = 0x8000; /* display on, no layer yet */
    REG16(CORAL_DISP + 0x2e) = 0;
    REG16(CORAL_DISP + 0x2c) = 0;
    REG16(CORAL_DISP + 0x56) = 0;
    REG16(CORAL_DISP + 0x54) = 0;
    REG16(CORAL_DISP + 0x86) = 0;
    REG16(CORAL_DISP + 0x84) = 0;
    REG16(CORAL_DISP + 0xc0) = 0x8000;
    REG16(CORAL_DISP + 0xba) = 0x8000;
    REG8(CORAL_HOST + 0x27) = 0x13;
    REG8(PORT3_DATA) = 0x00;

    /* A black picture, then the layer that shows it: 8 bits per pixel,
     * 448 bytes a line, at SURFACE in video memory. */
    for (i = 0; i < STRIDE * 240; i += 4)
        REG32(VRAM + SURFACE + i) = 0;
    REG32(CORAL_DISP + 0x70) = 0x00070400;
    REG32(CORAL_DISP + 0x74) = SURFACE;
    REG32(CORAL_DISP + 0x78) = SURFACE;
    REG16(CORAL_DISP + 0x86) = 0;
    REG16(CORAL_DISP + 0x84) = 0;
    REG32(CORAL_DISP + 0x40) = 0x00000400;
    REG32(CORAL_DISP + 0x44) = 0;
    REG32(CORAL_DISP + 0x48) = 0;
    REG16(CORAL_DISP + 0x56) = 0;
    REG16(CORAL_DISP + 0x54) = 0;
    REG32(CORAL_DISP + 0x20) = 0x00000400;
    REG32(CORAL_DISP + 0x24) = 0;
    REG32(CORAL_DISP + 0x28) = 0;
    REG16(CORAL_DISP + 0x2e) = 0;
    REG16(CORAL_DISP + 0x2c) = 0;
    REG16(CORAL_DISP + 0x00) = 0x8008; /* that layer on */

    /* The tick: timer 0 at a quarter of the 29.4 MHz peripheral clock,
     * 73500 counts = 10 ms, interrupt level 7. */
    REG16(TCR0) = 0x0020;
    REG32(TCOR0) = 0x00011f1b;
    REG32(TCNT0) = 0x00011f1b;
    REG16(IPRA) = 0x7000;
    REG8(TSTR) = 1;

    {
        extern void irq_enable(void);
        irq_enable();
    }
}
