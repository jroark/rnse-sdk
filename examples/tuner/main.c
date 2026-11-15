/* The tuner: a radio of the plainest kind, to find out on a unit what
 * the radio processor makes of the SDK's frames.  The 192.
 *
 *   select knob    the frequency, 200 kHz a detent (FM as in North
 *                  America: 87.9 to 107.9 MHz)
 *   NEXT, PREV     1 MHz
 *   volume knob    the volume
 *   RETURN         the unit restarts
 *
 * It starts at the frequency the radio processor says it is on.  What
 * the processor answers goes out on the CAN bus (0x7F8, test 0x31):
 * item 0 its status at the start (the frequency, and the source and
 * volume); 1 each frequency asked for, with the answer's command and
 * the result; 2 the status a second after the last change (the
 * frequency it says, and bytes 6 and 7); 3 the volume and the result;
 * 0x10 on, the whole status block once at the start. */
#include "rnse.h"

static int frequency = 2042, volume = 8;

static void line(int y, const char *text, u8 colour, int scale)
{
    screen_fill(0, y, screen_width, 8 * scale, 0);
    screen_text(8, y, text, colour, scale);
}

static void rec(u8 item, u32 value, u32 second)
{
    u8 d[8] = { 0x31, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, (u8)(second >> 8), (u8)second };
    can_send_for(0x7f8, d, 8, 3000);
}

static char *put_dec(char *p, u32 v)
{
    char t[10];
    int n = 0;
    do {
        t[n++] = (char)('0' + umod(v, 10));
        v = udiv(v, 10);
    } while (v);
    while (n)
        *p++ = t[--n];
    return p;
}

static void show(const char *note)
{
    char text[40], *p = text;
    u32 khz = (u32)frequency * 50;
    p = put_dec(p, udiv(khz, 1000));
    *p++ = '.';
    p = put_dec(p, udiv(umod(khz, 1000), 100));
    *p++ = ' ';
    *p++ = 'M';
    *p++ = 'H';
    *p++ = 'Z';
    *p = 0;
    line(60, text, 2, 4);
    p = text;
    *p++ = 'V';
    *p++ = 'O';
    *p++ = 'L';
    *p++ = ' ';
    p = put_dec(p, (u32)volume);
    *p = 0;
    line(120, text, 1, 2);
    line(160, note, 1, 2);
}

int main(void)
{
    u8 status[64];
    u32 changed = 0;
    int ask = 0;
    int n, i, r, was[2] = { 0 };
    board_init();
    console_puts("\nrnse sdk: tuner\n");
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 200, 0);
    can_init();
    line(8, "THE TUNER", 2, 2);
    n = radio_status(status, sizeof status);
    if (n >= 35) {
        frequency = status[4] << 8 | status[5];
        if (status[34] && status[34] <= 30)
            volume = status[34];
        rec(0, (u32)frequency << 16 | (u32)status[33] << 8 | status[34], (u32)n);
        for (i = 0; i < n; i += 4)
            rec((u8)(0x10 + i / 4), (u32)status[i] << 24 | (u32)status[i + 1] << 16 | (u32)status[i + 2] << 8 | status[i + 3], (u32)i);
    } else {
        rec(0, 0xffffffff, (u32)n);
    }
    if (frequency < 1758 || frequency > 2158)
        frequency = 2042;
    r = radio_source(RADIO_TUNER, volume);
    show(n < 0 ? "NO STATUS FROM THE RADIO" : r ? "SOURCE NOT TAKEN" : "TURN THE KNOB");
    panel_init();
    for (;;) {
        int key[2], turn;
        panel_poll();
        key[0] = panel_key(KEY_NEXT);
        key[1] = panel_key(KEY_PREV);
        turn = 4 * panel_knob(KNOB_SELECT) + 20 * ((key[0] && !was[0]) - (key[1] && !was[1]));
        was[0] = key[0];
        was[1] = key[1];
        if (turn) {
            frequency += turn;
            if (frequency > 2158)
                frequency = 1758;
            if (frequency < 1758)
                frequency = 2158;
            r = radio_tune(frequency);
            rec(1, (u32)frequency, (u32)(radio_answered << 8) | (u32)(r & 0xff));
            console_puts(r ? "tune: no answer\n" : "tuned\n");
            show(r ? "NOT ANSWERED" : radio_answered == 0x20 ? "ACKNOWLEDGED" : "ANSWERED");
            changed = millis();
            ask = 1;
        }
        turn = panel_knob(KNOB_VOLUME);
        if (turn) {
            volume += turn;
            volume = volume < 0 ? 0 : volume > 30 ? 30 : volume;
            rec(3, (u32)volume, (u32)radio_volume(volume));
            show("");
        }
        if (ask && millis() - changed > 1000) {
            ask = 0;
            n = radio_status(status, sizeof status);
            rec(2, n >= 8 ? (u32)status[4] << 24 | (u32)status[5] << 16 | (u32)status[6] << 8 | status[7] : 0xffffffff, (u32)n);
        }
        if (panel_key(KEY_RETURN))
            restart();
    }
}
