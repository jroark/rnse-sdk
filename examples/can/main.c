/* CAN: lists the frames that arrive and, once a second, sends the frame
 * with which the stock software reports itself on the bus (0x661). */
#include "rnse.h"

#define ROWS 9

static char *put_hex(char *p, u32 v, int digits)
{
    while (digits--)
        *p++ = "0123456789ABCDEF"[v >> (4 * digits) & 15];
    return p;
}

static void show(int row, u16 id, const u8 *data, int len, u8 colour)
{
    char text[40], *p = text;
    int s = screen_width / 400, i;
    p = put_hex(p, id, 3);
    *p++ = ':';
    for (i = 0; i < len; i++) {
        *p++ = ' ';
        p = put_hex(p, data[i], 2);
    }
    *p = 0;
    screen_fill(8 * s, (28 + 20 * row) * s, screen_width - 16 * s, 14 * s, 0);
    screen_text(8 * s, (28 + 20 * row) * s, text, colour, 2 * s);
    console_puts(colour == 2 ? "tx " : "rx ");
    console_puts(text);
    console_puts("\n");
}

int main(void)
{
    static const u8 status[8] = { 0x83, 0x01, 0x12, 0xa0, 0, 0, 0, 0 };
    u32 next = 1000;
    int row = 0, s;

    board_init();
    console_puts("\nrnse sdk: can\n");
    s = screen_width / 400;
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 96, 0);
    screen_palette(3, 120, 120, 120);
    screen_text(8 * s, 6 * s, "CAN", 1, 2 * s);
    can_init();
    for (;;) {
        u16 id;
        u8 data[8];
        int len = can_receive(&id, data);
        if (len >= 0) {
            show(row, id, data, len, 1);
            row = (row + 1) % ROWS;
        } else if (millis() >= next) {
            next += 1000;
            if (can_send(0x661, status, 8) == 0) {
                show(row, 0x661, status, 8, 2);
                row = (row + 1) % ROWS;
            } else {
                screen_fill(60 * s, 6 * s, 300 * s, 14 * s, 0);
                screen_text(60 * s, 6 * s, "NOBODY ANSWERS", 3, 2 * s);
                console_puts("tx not acknowledged\n");
            }
        } else {
            cpu_idle();
        }
    }
}
