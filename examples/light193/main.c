/* What lights a bench 193's screen again: a diagnostic for the CAN push
 * (sdload193 starts it).  No radio keep-alive; every 8 s one step, said
 * on CAN 0x7F8 as record 0xe5 (item: the step; value: what the step
 * answered; second: whether the faceplate controller answers, 1 = it
 * does), and on the screen:
 *
 *   0  nothing (8 s to look)
 *   1  the faceplate controller started again (panel_init)
 *   2  the radio processor: 80 [08 00]
 *   3  80 [02 01]
 *   4  80 [00 02]
 *   5  17 [01]
 *   6  8a (the 193's repeated frame)
 *
 * then it waits, the keys answered (display key, CD EJECT, RETURN held:
 * back to the loader). */
#include "rnse.h"

static void rec(u8 item, u32 value)
{
    u8 d[8] = { 0xe5, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, 0, (u8)panel_online() };
    can_send_for(0x7f8, d, 8, 3000);
}

static void wait_ms(u32 ms)
{
    u32 t = millis();
    while (millis() - t < ms) {
        panel_service();
        cpu_idle();
    }
}

static void show(int step, const char *what)
{
    char text[60], *p = text;
    const char *m = "STEP ";
    while (*m)
        *p++ = *m++;
    *p++ = (char)('0' + step);
    *p++ = ':';
    *p++ = ' ';
    while (*what)
        *p++ = *what++;
    *p = 0;
    screen_fill(0, 40 + 24 * step, screen_width, 24, 0);
    screen_text(16, 40 + 24 * step, text, 1, 2);
    console_puts(text);
    console_puts("\n");
}

int main(void)
{
    static const u8 c80a[2] = { 0x08, 0x00 }, c80b[2] = { 0x02, 0x01 }, c80c[2] = { 0x00, 0x02 }, c17[1] = { 0x01 };
    u8 a[64];
    int r;
    radio_keepalive_off = 1;
    board_init();
    console_puts("\nrnse sdk: light193\n");
    screen_palette(1, 255, 255, 255);
    screen_fill(0, 0, screen_width, screen_height, 0);
    screen_text(16, 8, "LIGHT193: WHAT LIGHTS THE SCREEN.  CAN 7F8, RECORD E5", 1, 2);
    panel_attach();
    sd_idle = panel_service;
    display_idle = panel_service;
    show(0, "NOTHING");
    rec(0, 0);
    wait_ms(8000);
    show(1, "FACEPLATE STARTED AGAIN");
    panel_init();
    rec(1, 0);
    wait_ms(8000);
    show(2, "RADIO 80 [08 00]");
    r = radio_command(0x80, c80a, 2, a, sizeof a);
    rec(2, (u32)r << 8 | (u8)radio_answered);
    wait_ms(8000);
    show(3, "RADIO 80 [02 01]");
    r = radio_command(0x80, c80b, 2, a, sizeof a);
    rec(3, (u32)r << 8 | (u8)radio_answered);
    wait_ms(8000);
    show(4, "RADIO 80 [00 02]");
    r = radio_command(0x80, c80c, 2, a, sizeof a);
    rec(4, (u32)r << 8 | (u8)radio_answered);
    wait_ms(8000);
    show(5, "RADIO 17 [01]");
    r = radio_command(0x17, c17, 1, a, sizeof a);
    rec(5, (u32)r << 8 | (u8)radio_answered);
    wait_ms(8000);
    show(6, "RADIO 8A");
    r = radio_command(0x8a, 0, 0, a, sizeof a);
    rec(6, (u32)r << 8 | (u8)radio_answered);
    for (;;) {
        wait_ms(5000);
        rec(9, millis());
    }
    return 0;
}
