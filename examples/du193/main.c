/* The 193's display unit as the stock software leaves it, and as the
 * kit's board_init leaves it: on the bench 193 the screen stays black
 * (lit) under the kit's set-up, though the kit draws into its picture
 * (2026-11-20), so what the stock software has there is read and sent.
 * Then a test pattern is drawn in stages (main), each changing the
 * display unit a little more toward what the unit's own software
 * writes.  CD EJECT works (the kit's panel_service); after some 32 s
 * the unit starts again.  Nothing is
 * written before the first read.
 *
 * Records on 0x7F8 (test, item, value, second):
 *   0xe0 r   before board_init: region r's word at offset `second`
 *   0xe1 r   the same after board_init
 *            regions: 0 0xfff80000 + 0..0x7c (control), 1..8 the
 *            planes 0..7 (0xfff80100 * n .. + 0x3c), 9 0xfff90000 ..
 *            + 0x3c, 10 the palettes' first words (0xfff81000 * k)
 *   0xe2 0   alive; 0xe2 1 board_init done
 *   0xe3 X   stage X shown (see main): two of its registers
 *   0xff 0xff the end */
#include "rnse.h"

#define CAN 0xffdf0000
#define CAN_MB1 (CAN + 0x100 + 0x20)
#define WATCHDOG 0xffc44008 /* bit 0: the external watchdog's strobe (hw193.c) */

/* The watchdog strobed while nothing else does it (before board_init's
 * tick), the port's other bits as they are. */
static void feed(void)
{
    static u32 strobe;
    strobe ^= 1;
    REG32(WATCHDOG) = (REG32(WATCHDOG) & ~1u) | strobe;
}

static void say(u8 test, u8 item, u32 value, u32 second)
{
    u8 d[8] = { test, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, (u8)(second >> 8), (u8)second };
    u32 n;
    int i;
    REG16(CAN_MB1 + 0x00) = (u16)(0x7f8 << 2);
    for (i = 0; i < 8; i++)
        REG8(CAN_MB1 + 0x08 + i) = d[i];
    REG16(CAN_MB1 + 0x10) = 8;
    REG32(CAN + 0x20) = 0x00000002;
    for (n = 0; n < 400000; n++)
        if (REG16(CAN + 0x32) & 0x0002)
            break;
    if (REG16(CAN + 0x32) & 0x0002) {
        REG16(CAN + 0x32) = 0x0002;
    } else {
        REG32(CAN + 0x28) = 0x00000002;
        for (n = 0; n < 400000; n++)
            if (!(REG16(CAN + 0x22) & 0x0002) || (REG16(CAN + 0x3a) & 0x0002))
                break;
        REG16(CAN + 0x3a) = 0x0002;
    }
    /* Some room on the bus for the logger (a counted wait: no timer
     * before board_init). */
    for (n = 0; n < 20000; n++)
        __asm__ volatile("nop");
    feed();
}

static void region(u8 test, u8 r, u32 base, u32 words)
{
    u32 i;
    for (i = 0; i < words; i++)
        say(test, r, REG32(base + 4 * i), 4 * i);
}

static void dump(u8 test)
{
    int n;
    region(test, 0, 0xfff80000, 32);
    for (n = 0; n < 8; n++)
        region(test, (u8)(1 + n), 0xfff80000 + 0x100 * (u32)n + (n == 0 ? 0x80 : 0), 16);
    region(test, 9, 0xfff90000, 16);
    for (n = 0; n < 4; n++)
        say(test, 10, REG32(0xfff81000 + 0x1000 * (u32)n), (u32)n);
    for (n = 0; n < 4; n++)
        say(test, 10, REG32(0xfff81000 + 0x1000 * (u32)n + 4), 0x10 + (u32)n);
}

/* The pattern, and the stage's letter big in the middle. */
static void pattern(const char *stage)
{
    int i;
    for (i = 0; i < 8; i++)
        screen_palette(2 + i, (u8)(i & 1 ? 255 : 0), (u8)(i & 2 ? 255 : 0), (u8)(i & 4 ? 255 : 0));
    screen_palette(0, 0, 0, 0);
    screen_palette(1, 255, 255, 255);
    for (i = 0; i < 8; i++)
        screen_fill(i * screen_width / 8, 200, screen_width / 8, screen_height - 200, (u8)(2 + i));
    screen_fill(0, 0, screen_width, 200, 0);
    screen_text(16, 16, "DU193 STAGE", 1, 4);
    screen_text(320, 16, stage, 1, 20);
}

static void hold(u32 ms)
{
    u32 end = millis() + ms;
    while ((s32)(millis() - end) < 0) {
        panel_service();
        cpu_idle();
    }
}

#define DU 0xfff80000

/* Stages, each kept for 6 s, each adding to the one before what the
 * unit's own software (V5751050, emulated) writes and the kit does not:
 *   A  the kit's set-up as it is
 *   B  DSMR (+0x04) 0x0700f000 rather than 0x07000000
 *   C  DSYSR bit 16 set once running (the software's 0x00110100)
 *   D  +0x94 0x00f800f0 and +0x24 0x77730011
 *   E  the software's display timing (HDSR .. VDER) */
int main(void)
{
    REG32(CAN + 0x28) = 0xfffffffe;
    say(0xe2, 0, 0, 0);
    dump(0xe0);
    board_init();
    say(0xe2, 1, millis(), 0);
    dump(0xe1);
    console_puts("\nrnse sdk: du193\n");
    panel_init();

    pattern("A");
    say(0xe3, 'A', REG32(DU + 0x04), REG32(DU + 0x00) >> 8);
    hold(6000);

    REG32(DU + 0x00) = 0x00100000;   /* stopped while it changes */
    REG32(DU + 0x04) = 0x0700f000;
    REG32(DU + 0x00) = 0x00100100;
    pattern("B");
    say(0xe3, 'B', REG32(DU + 0x04), REG32(DU + 0x00) >> 8);
    hold(6000);

    REG32(DU + 0x00) = REG32(DU + 0x00) | 0x00010000;
    pattern("C");
    say(0xe3, 'C', REG32(DU + 0x04), REG32(DU + 0x00) >> 8);
    hold(6000);

    REG32(DU + 0x94) = 0x00f800f0;
    REG32(DU + 0x24) = 0x77730011;
    pattern("D");
    say(0xe3, 'D', REG32(DU + 0x94), REG32(DU + 0x24));
    hold(6000);

    REG32(DU + 0x00) = 0x00100000;
    REG32(DU + 0x40) = 0x0c5;
    REG32(DU + 0x44) = 0x405;
    REG32(DU + 0x48) = 0x020;
    REG32(DU + 0x4c) = 0x210;
    REG32(DU + 0x00) = 0x00110100;
    pattern("E");
    say(0xe3, 'E', REG32(DU + 0x40), REG32(DU + 0x00) >> 8);
    hold(8000);

    say(0xff, 0xff, millis(), 0);
    restart();
    return 0;
}
