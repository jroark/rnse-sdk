/* The program to try first on a unit: a test picture and a count-down,
 * after which it hands over to the stock loader.  The unit is then
 * updated from the disc like from any update disc and is its ordinary
 * self again, with the disc no longer counting as an update.  Nothing
 * has to work for that but the processor and its memory: no key, no
 * drive command of this program's, no picture.
 *
 * WAIT seconds are shown; change it to taste. */
#include "rnse.h"

#define WAIT 60

static void number(int x, int y, u32 v, int scale, u8 colour)
{
    char text[11];
    int n = 10;
    text[n] = 0;
    do {
        text[--n] = (char)('0' + umod(v, 10));
        v = udiv(v, 10);
    } while (v);
    screen_fill(x, y, 6 * 4 * scale, 7 * scale, 0);
    screen_text(x, y, text + n, colour, scale);
}

int main(void)
{
    static const u8 bars[8][3] = {
        { 255, 255, 255 }, { 255, 255, 0 }, { 0, 255, 255 }, { 0, 255, 0 },
        { 255, 0, 255 },   { 255, 0, 0 },   { 0, 0, 255 },   { 40, 40, 40 },
    };
    int i, w, h, s, carried, left;

    board_init();
    console_puts("\nrnse sdk: probe\n");
    carried = stock_loader_carried();
    console_puts(carried ? "the stock loader is carried along\n" : "NO STOCK LOADER IN THIS IMAGE\n");

    w = screen_width;
    h = screen_height;
    s = w / 400;
    for (i = 0; i < 8; i++) {
        screen_palette(1 + i, bars[i][0], bars[i][1], bars[i][2]);
        screen_fill(i * w / 8, 0, w / 8, h / 4, (u8)(1 + i));
    }
    screen_palette(9, 255, 255, 255);
    screen_palette(10, 255, 96, 0);
    screen_fill(0, 0, w, 1, 9);
    screen_fill(0, h - 1, w, 1, 9);
    screen_fill(0, 0, 1, h, 9);
    screen_fill(w - 1, 0, 1, h, 9);
    screen_text(12 * s, 72 * s, "RNSE SDK: PROBE", 9, 3 * s);
    screen_text(12 * s, 104 * s, "LANGUAGE", 9, 2 * s);
    number(120 * s, 104 * s, start_args[0], 2 * s, 9);
    if (carried) {
        screen_text(12 * s, 136 * s, "STOCK UPDATE STARTS IN", 10, 2 * s);
    } else {
        /* Built into a disc by something other than rnse custom, or
         * started without a disc at all (rnse loader with the bare
         * program): there is nothing to hand over to. */
        screen_text(12 * s, 136 * s, "NO STOCK LOADER: NO WAY BACK", 10, 2 * s);
        for (;;)
            cpu_idle();
    }
    for (left = WAIT; left > 0; left--) {
        number(300 * s, 136 * s, (u32)left, 2 * s, 10);
        console_puts("stock update in ");
        console_dec((u32)left);
        console_puts("\n");
        delay_ms(1000);
    }
    screen_fill(12 * s, 136 * s, w - 24 * s, 14 * s, 0);
    screen_text(12 * s, 136 * s, "STARTING THE STOCK LOADER", 10, 2 * s);
    console_puts("starting the stock loader\n");
    i = stock_update();
    /* Only when that failed. */
    screen_fill(12 * s, 136 * s, w - 24 * s, 14 * s, 0);
    screen_text(12 * s, 136 * s, "THE STOCK LOADER DID NOT UNPACK", 10, 2 * s);
    console_puts("the stock loader did not unpack: ");
    console_hex((u32)i);
    console_puts("\n");
    for (;;)
        cpu_idle();
}
