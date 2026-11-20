/* The car (src/car.c): the ignition lock, the steering wheel's keys as
 * the program sees them through the faceplate's (volume knob, NEXT,
 * PREV) and raw, and the radio's two lines in the instrument cluster.
 *
 * Everything it sees also goes out on 0x7f8, for a check from the
 * other end of the bus (tools/canbench.py) or a test:
 *     'I' bits          the ignition lock changed (0xff: no message)
 *     'W' code          a wheel key went down (raw code)
 *     'V' hi lo         the volume knob's total so far, two's complement
 *     'K' key n         KEY_NEXT or KEY_PREV pressed, n times so far
 * The display key, CD EJECT and RETURN held for two seconds work as in
 * every program. */
#include "rnse.h"

static int s;

static char *put_dec(char *p, s32 v)
{
    char t[11];
    int n = 0;
    u32 u = v < 0 ? (u32)-v : (u32)v;
    if (v < 0)
        *p++ = '-';
    do {
        t[n++] = (char)('0' + umod(u, 10));
        u = udiv(u, 10);
    } while (u);
    while (n)
        *p++ = t[--n];
    *p = 0;
    return p;
}

static char *put_str(char *p, const char *q)
{
    while (*q)
        *p++ = *q++;
    *p = 0;
    return p;
}

static char *put_hex2(char *p, u32 v)
{
    *p++ = "0123456789ABCDEF"[v >> 4 & 15];
    *p++ = "0123456789ABCDEF"[v & 15];
    *p = 0;
    return p;
}

static void line(int row, const char *text, u8 colour)
{
    screen_fill(8 * s, (34 + 22 * row) * s, screen_width - 16 * s, 14 * s, 0);
    screen_text(8 * s, (34 + 22 * row) * s, text, colour, 2 * s);
}

static void report(u8 a, u8 b, u8 c, u8 d)
{
    u8 f[4];
    f[0] = a;
    f[1] = b;
    f[2] = c;
    f[3] = d;
    can_send_for(0x7f8, f, 4, 5000);
}

static const char *wheel_name(int code)
{
    switch (code) {
    case WHEEL_SEEK_UP: return "SEEK UP";
    case WHEEL_SEEK_DOWN: return "SEEK DOWN";
    case WHEEL_VOLUME_UP: return "VOL UP";
    case WHEEL_VOLUME_DOWN: return "VOL DOWN";
    case 0: return "-";
    default: return "?";
    }
}

int main(void)
{
    char text[40], *p;
    int ign = -2, held = -1, volume = 0, next = 0, prev = 0, code;
    char last[9] = "WAITING";

    board_init();
    console_puts("\nrnse sdk: car\n");
    s = screen_width / 400;
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 96, 0);
    screen_palette(3, 120, 120, 120);
    screen_text(8 * s, 6 * s, "CAR", 1, 2 * s);
    panel_init();
    car_init();
    car_cluster_text("RNSE SDK", last);
    line(2, "VOLUME 0", 1);
    line(3, "NEXT 0  PREV 0", 1);
    for (;;) {
        int k, now;
        panel_service(); /* and with it car_poll */
        now = car_ignition();
        if (now != ign) {
            ign = now;
            p = put_str(text, "IGNITION ");
            put_str(p, now < 0 ? "NO MESSAGE" : now & CAR_IGNITION ? "ON" : now & CAR_KEY_IN ? "KEY IN" : "OFF");
            line(0, text, now < 0 ? 3 : 1);
            console_puts(text);
            console_puts("\n");
            report('I', (u8)(now < 0 ? 0xff : now), 0, 0);
        }
        now = car_wheel();
        if (now != held) {
            held = now;
            p = put_str(text, "WHEEL ");
            p = put_str(p, wheel_name(now));
            if (now) {
                p = put_str(p, " ");
                put_hex2(p, (u32)now);
            }
            line(1, text, now ? 2 : 1);
        }
        while ((code = car_wheel_next()) != 0) {
            p = put_str(text, "wheel ");
            p = put_hex2(p, (u32)code);
            put_str(p, "\n");
            console_puts(text);
            report('W', (u8)code, 0, 0);
        }
        if ((k = panel_knob(KNOB_VOLUME)) != 0) {
            volume += k;
            p = put_str(text, "VOLUME ");
            put_dec(p, volume);
            line(2, text, 1);
            console_puts(text);
            console_puts("\n");
            report('V', (u8)((u32)volume >> 8), (u8)volume, 0);
            p = put_str(last, "VOL ");
            put_dec(p, volume);
            car_cluster_text("RNSE SDK", last);
        }
        k = 0;
        if (panel_pressed(KEY_NEXT)) {
            next++;
            report('K', KEY_NEXT, (u8)next, 0);
            put_str(last, "NEXT");
            k = 1;
        }
        if (panel_pressed(KEY_PREV)) {
            prev++;
            report('K', KEY_PREV, (u8)prev, 0);
            put_str(last, "PREV");
            k = 1;
        }
        if (k) {
            p = put_str(text, "NEXT ");
            p = put_dec(p, next);
            p = put_str(p, "  PREV ");
            put_dec(p, prev);
            line(3, text, 1);
            console_puts(text);
            console_puts("\n");
            car_cluster_text("RNSE SDK", last);
        }
        cpu_idle();
    }
}
