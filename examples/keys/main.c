/* Keys and knobs: shows what the faceplate reports, on the screen and
 * on the debug serial port. */
#include "rnse.h"

static const struct {
    u8 key;
    const char *name;
} names[] = {
    { KEY_RADIO, "RADIO" },    { KEY_MEDIA, "MEDIA" },     { KEY_NAME, "NAME" },       { KEY_TEL, "TEL" },
    { KEY_NAV, "NAV" },        { KEY_INFO, "INFO" },       { KEY_CAR, "CAR" },         { KEY_SETUP, "SETUP" },
    { KEY_RETURN, "RETURN" },  { KEY_PREV, "PREV" },       { KEY_NEXT, "NEXT" },       { KEY_SELECT, "SELECT" },
    { KEY_SOFT_TL, "SOFT TL" }, { KEY_SOFT_TR, "SOFT TR" }, { KEY_SOFT_BL, "SOFT BL" }, { KEY_SOFT_BR, "SOFT BR" },
};
#define KEYS ((int)(sizeof names / sizeof names[0]))

static void number(int x, int y, int v, int scale)
{
    char text[12];
    int n = 11, negative = v < 0;
    u32 u = negative ? (u32)-v : (u32)v;
    text[n] = 0;
    do {
        text[--n] = (char)('0' + umod(u, 10));
        u = udiv(u, 10);
    } while (u);
    if (negative)
        text[--n] = '-';
    screen_fill(x, y, 6 * 6 * scale, 7 * scale, 0);
    screen_text(x, y, text + n, 2, scale);
}

int main(void)
{
    int w, s, i, down[KEYS], position[2] = { 0, 0 }, online = 0;

    board_init();
    console_puts("\nrnse sdk: keys\n");
    w = screen_width;
    s = w / 400; /* dot size: 1 on the small screen, 2 on the large */
    screen_palette(1, 90, 90, 90);    /* a key that is up */
    screen_palette(2, 255, 255, 255); /* text */
    screen_palette(3, 255, 96, 0);    /* a key that is down */
    screen_text(8 * s, 6 * s, "FACEPLATE: WAITING", 2, 2 * s);
    for (i = 0; i < KEYS; i++) {
        down[i] = 0;
        screen_text((8 + (i & 3) * 98) * s, (40 + (i >> 2) * 24) * s, names[i].name, 1, 2 * s);
    }
    screen_text(8 * s, 150 * s, "VOLUME", 2, 2 * s);
    screen_text(208 * s, 150 * s, "SELECT", 2, 2 * s);
    number(100 * s, 150 * s, 0, 2 * s);
    number(300 * s, 150 * s, 0, 2 * s);

    panel_init();
    for (;;) {
        panel_poll();
        if (panel_online() != online) {
            online = panel_online();
            screen_fill(8 * s, 6 * s, 380 * s, 14 * s, 0);
            screen_text(8 * s, 6 * s, online ? "FACEPLATE: ONLINE" : "FACEPLATE: WAITING", 2, 2 * s);
            console_puts(online ? "faceplate online\n" : "faceplate lost\n");
        }
        for (i = 0; i < KEYS; i++) {
            int now = panel_key(names[i].key);
            if (now != down[i]) {
                down[i] = now;
                screen_text((8 + (i & 3) * 98) * s, (40 + (i >> 2) * 24) * s, names[i].name, now ? 3 : 1, 2 * s);
                console_puts(names[i].name);
                console_puts(now ? " down\n" : " up\n");
            }
        }
        for (i = 0; i < 2; i++) {
            int turned = panel_knob(i);
            if (turned) {
                position[i] += turned;
                number((100 + 200 * i) * s, 150 * s, position[i], 2 * s);
                console_puts(i ? "select " : "volume ");
                console_dec((u32)(position[i] < 0 ? -position[i] : position[i]));
                console_puts(position[i] < 0 ? " left\n" : " right\n");
            }
        }
        cpu_idle();
    }
}
