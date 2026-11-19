/* The probe for a unit whose screen stays dark: it says how far it has
 * got on the CAN bus, where a logic analyser on the two wires can read
 * it without anybody acknowledging anything.
 *
 * A unit alone on a bench repeats the last frame of its installed
 * software for ever, because nobody acknowledges it; that goes on by
 * itself while a program in the loader's place runs.  So:
 *
 *   - the first thing this program does, before it sets anything up, is
 *     to wake the CAN transceiver (192: bits 3 and 2 of port A, which
 *     the installed software clears when it lets the bus sleep, as it
 *     does after a few seconds alone; a unit that shows the radio on a
 *     bench has a sleeping transceiver and nothing it sends gets out)
 *     and to take that frame back: the two lines rise and the bus goes
 *     quiet at the moment the program starts;
 *   - when the board is up it sends frames 0x7F6, each for a tenth of a
 *     second: once with stage 1, then one a second with stage 2 and the
 *     seconds left (their spacing is the SDK's clock, measured);
 *     0x7F7 carries the state the program was started in;
 *   - before it hands over to the stock loader it leaves a frame with
 *     stage 3 in the controller (192), which goes on repeating through
 *     the hand-over until the stock loader sets the controller up.
 *
 * Half way through the wait it starts the faceplate controller, which
 * board_init leaves in reset as the stock loader does, and gives it
 * its settings, the lighting among them: stage 5, and from then on
 * bit 1 of the last byte says whether the controller has taken them.
 * A screen that is dark until then and shows the picture afterwards
 * has its lighting from that controller.
 *
 * 0x7F6: stage, seconds left, the clock in 10 ms (two bytes), the
 * display controller's mode register read back (two bytes; 192), the
 * language argument, then bit 0 = the stock loader is carried along,
 * bit 1 = the faceplate controller has its settings.
 * 0x7F7: 0..3 = SR, VBR, the caller, the stack pointer at the start,
 * 4..6 = the arguments, 7 = the latch at 0xb6000004, 8 = port A, both
 * as the program found them; then the value.
 *
 * DARK seconds without the faceplate and LIT seconds with it
 * (EXTRA_CFLAGS=-DDARK=n -DLIT=n), then the way back, as in the probe.
 * The picture is the probe's. */
#include "rnse.h"

#ifndef DARK
#define DARK 8
#endif
#ifndef LIT
#define LIT 14
#endif
#define WAIT (DARK + LIT)

/* The 192's CAN controller (src/hw192.c). */
#define CAN 0xa7c00000
#define CAN_MB1 (CAN + 0x120)
#define PORT_A 0xb2a00001
#define LATCH16 0xb6000004

static void beacon(u8 stage, u32 left, int carried, int stay)
{
    u8 d[8];
    u32 t = udiv(millis(), 10);
    u16 mode = screen_width == 400 ? REG16(0xb9fd0000) : 0;
    int i;
    d[0] = stage;
    d[1] = (u8)left;
    d[2] = (u8)(t >> 8);
    d[3] = (u8)t;
    d[4] = (u8)(mode >> 8);
    d[5] = (u8)mode;
    d[6] = (u8)start_args[0];
    d[7] = (u8)(carried | panel_online() << 1);
    if (stay && screen_width == 400) {
        /* Asked for and left alone: the controller repeats it. */
        REG16(CAN_MB1 + 0x00) = (u16)(0x7f6 << 4);
        REG16(CAN_MB1 + 0x02) = 0;
        for (i = 0; i < 8; i++)
            REG8(CAN_MB1 + 0x08 + i) = d[i];
        REG16(CAN_MB1 + 0x04) = 0x2008;
        REG16(CAN + 0x22) = 0x0002;
    } else {
        can_send(0x7f6, d, 8);
    }
}

/* The CAN controller's first eight registers, for a photograph: the
 * frames above have not been seen on the wires of a real unit yet. */
static void can_line(int y, int s)
{
    static const char hex[] = "0123456789ABCDEF";
    char text[41];
    int i, k, n = 0;
    if (screen_width != 400)
        return;
    for (i = 0; i < 8; i++) {
        u16 v = REG16(CAN + 2 * i);
        for (k = 12; k >= 0; k -= 4)
            text[n++] = hex[v >> k & 15];
        text[n++] = ' ';
    }
    text[n] = 0;
    screen_fill(0, y, screen_width, 8 * s, 0);
    screen_text(4 * s, y, text, 9, s);
}

static void word(u8 n, u32 v)
{
    u8 d[8] = { n, 0, 0, 0, (u8)(v >> 24), (u8)(v >> 16), (u8)(v >> 8), (u8)v };
    can_send(0x7f7, d, 8);
}

int main(void)
{
    static const u8 bars[8][3] = {
        { 255, 255, 255 }, { 255, 255, 0 }, { 0, 255, 255 }, { 0, 255, 0 },
        { 255, 0, 255 },   { 255, 0, 0 },   { 0, 0, 255 },   { 40, 40, 40 },
    };
    int i, w, h, s, carried, left;
    u32 latch = 0, port = 0;

    if (screen_width == 400) {
        latch = REG32(LATCH16);
        port = REG8(PORT_A);
        /* The transceiver out of its sleep, as the installed software
         * does it: both bits at once. */
        REG8(PORT_A) = (u8)(port | 0x0c);
        /* Every transmit request of the installed software taken back. */
        REG16(CAN + 0x28) = 0xffff;
        REG16(CAN + 0x2a) = 0xfffe;
    }

    board_init();
    console_puts("\nrnse sdk: beacon\n");
    carried = stock_loader_carried();
    can_init();
    beacon(1, WAIT, carried, 0);
    for (i = 0; i < 4; i++)
        word((u8)i, start_state[i]);
    for (i = 0; i < 3; i++)
        word((u8)(4 + i), start_args[i]);
    word(7, latch);
    word(8, port);

    w = screen_width;
    h = screen_height;
    s = w / 400;
    for (i = 0; i < 8; i++) {
        screen_palette(1 + i, bars[i][0], bars[i][1], bars[i][2]);
        screen_fill(i * w / 8, 0, w / 8, h / 4, (u8)(1 + i));
    }
    screen_palette(9, 255, 255, 255);
    screen_palette(10, 255, 96, 0);
    screen_text(12 * s, 72 * s, "RNSE SDK: BEACON", 9, 3 * s);
    screen_text(12 * s, 104 * s, "PROGRESS ON CAN 7F6", 9, 2 * s);
    if (!carried) {
        screen_text(12 * s, 136 * s, "NO STOCK LOADER: NO WAY BACK", 10, 2 * s);
        for (;;) {
            beacon(9, 0, 0, 0);
            delay_ms(900);
        }
    }
    screen_text(12 * s, 136 * s, "STOCK UPDATE FOLLOWS", 10, 2 * s);
    can_line(188 * s, s);
    for (left = WAIT; left > 0; left--) {
        u32 next = millis() + 1000;
        if (left == LIT) {
            beacon(5, (u32)left, carried, 0);
            panel_init();
            screen_text(12 * s, 168 * s, "FACEPLATE STARTED", 9, 2 * s);
        }
        beacon(2, (u32)left, carried, 0);
        can_line(200 * s, s);
        console_puts("stock update in ");
        console_dec((u32)left);
        console_puts("\n");
        while ((s32)(millis() - next) < 0) {
            if (left <= LIT)
                panel_poll();
            cpu_idle();
        }
    }
    console_puts("starting the stock loader\n");
    beacon(3, 0, carried, 1);
    i = stock_update();
    /* Only when that failed. */
    console_puts("the stock loader did not unpack: ");
    console_hex((u32)i);
    console_puts("\n");
    for (;;) {
        beacon(4, (u32)-i, carried, 0);
        delay_ms(900);
    }
}
