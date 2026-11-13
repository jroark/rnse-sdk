/* A first program for the RNS-E: a test picture, a line of text, a
 * running counter on the screen and on the debug serial port. */
#include "rnse.h"

int main(void)
{
    static const u8 bars[8][3] = {
        { 255, 255, 255 }, { 255, 255, 0 }, { 0, 255, 255 }, { 0, 255, 0 },
        { 255, 0, 255 },   { 255, 0, 0 },   { 0, 0, 255 },   { 40, 40, 40 },
    };
    int i, w, h;
    u32 seconds = 0;

    board_init();
    console_puts("\nrnse sdk: hello\n");

    w = screen_width;
    h = screen_height;
    for (i = 0; i < 8; i++) {
        screen_palette(1 + i, bars[i][0], bars[i][1], bars[i][2]);
        screen_fill(i * w / 8, 0, w / 8, h / 3, (u8)(1 + i));
    }
    screen_palette(9, 255, 255, 255);
    screen_palette(10, 255, 96, 0);
    /* A one-pixel frame on the panel's very edge: shows whether the
     * picture is where it should be. */
    screen_fill(0, 0, w, 1, 9);
    screen_fill(0, h - 1, w, 1, 9);
    screen_fill(0, 0, 1, h, 9);
    screen_fill(w - 1, 0, 1, h, 9);
    screen_text(w / 16, h / 2 - 4 * (w / 200), "HELLO, RNS-E", 9, w / 100);
    screen_text(w / 16, h * 3 / 4, "SECONDS:", 10, w / 200);

    for (;;) {
        char text[11];
        int n = 10;
        u32 v = seconds;
        text[n] = 0;
        do {
            text[--n] = (char)('0' + umod(v, 10));
            v = udiv(v, 10);
        } while (v);
        screen_fill(w / 16 + 54 * (w / 200), h * 3 / 4, 60 * (w / 200), 7 * (w / 200), 0);
        screen_text(w / 16 + 54 * (w / 200), h * 3 / 4, text + n, 10, w / 200);
        console_puts("tick ");
        console_dec(seconds);
        console_puts("\n");
        delay_ms(1000);
        seconds++;
    }
}
