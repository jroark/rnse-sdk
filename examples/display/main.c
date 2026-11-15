/* The fold-down display: the key next to the power button (KEY_DISPLAY)
 * opens and closes it, as on the stock software, and CD EJECT behind it
 * ejects the disc (panel_service).  Shows the position, the state and
 * the end switch, on the screen and on the debug serial port; NEXT
 * opens, PREV closes, SELECT stops the motor.  RETURN held for two
 * seconds goes back to the SD loader, when there is one; started any
 * other way, the program says so and goes on. */
#include "rnse.h"

static const char *const names[] = { "CLOSED", "OPEN", "BETWEEN", "OPENING", "CLOSING", "STUCK" };
static int s;

static void line(int row, const char *what, int v)
{
    char text[40], *p = text, t[12];
    int n = 0, y = (40 + 22 * row) * s;
    u32 u = v < 0 ? (u32)-v : (u32)v;
    while (*what)
        *p++ = *what++;
    *p++ = ' ';
    if (v < 0)
        *p++ = '-';
    do {
        t[n++] = (char)('0' + umod(u, 10));
        u = udiv(u, 10);
    } while (u);
    while (n)
        *p++ = t[--n];
    *p = 0;
    screen_fill(0, y, screen_width, 14 * s, 0);
    screen_text(8 * s, y, text, 2, 2 * s);
}

static void state_line(int state)
{
    int y = 40 * s;
    screen_fill(0, y, screen_width, 14 * s, 0);
    screen_text(8 * s, y, names[state], 3, 2 * s);
    console_puts("display ");
    {
        const char *n = names[state];
        char c[2] = { 0, 0 };
        while (*n) {
            c[0] = (char)(*n >= 'A' && *n <= 'Z' ? *n + 32 : *n);
            console_puts(c);
            n++;
        }
    }
    console_puts(", position ");
    console_dec((u32)display_position());
    console_puts("\n");
}

static int pressed(int key, int *was)
{
    int down = panel_key(key), edge = down && !*was;
    *was = down;
    return edge;
}

int main(void)
{
    int state = -1, pos = -2, sw = -1, next = 0, prev = 0, select = 0, held = 0, return_on = 0;
    u32 shown = 0, return_since = 0;

    board_init();
    console_puts("\nrnse sdk: display\n");
    s = screen_width / 400;
    screen_palette(1, 90, 90, 90);
    screen_palette(2, 255, 255, 255);
    screen_palette(3, 255, 96, 0);
    screen_text(8 * s, 6 * s, "DISPLAY: KEY BY THE POWER BUTTON", 2, 2 * s);
    screen_text(8 * s, screen_height - 20 * s, "NEXT OPEN  PREV CLOSE  SELECT STOP  HOLD RETURN: LOADER", 1, s);
    panel_init();
    display_idle = panel_service;
    for (;;) {
        int now;
        panel_service();
        if (panel_key(KEY_RETURN)) {
            if (!return_on) {
                return_on = 1;
                return_since = millis();
            }
            if (!held && millis() - return_since >= 2000) {
                held = 1;
                /* Back to the loader with the motor at rest; returns
                 * only when there is no loader session. */
                display_wait(10000);
                console_puts("display: done\n");
                loader_return(0);
                console_puts("return unavailable\n");
                screen_fill(0, (40 + 22 * 4) * s, screen_width, 14 * s, 0);
                screen_text(8 * s, (40 + 22 * 4) * s, "NO LOADER TO RETURN TO", 3, 2 * s);
            }
        } else {
            return_on = 0;
            held = 0;
        }
        if (pressed(KEY_NEXT, &next))
            display_open();
        if (pressed(KEY_PREV, &prev))
            display_close();
        if (pressed(KEY_SELECT, &select))
            display_stop();
        now = display_state();
        if (now != state) {
            if (state == DISPLAY_OPENING || state == DISPLAY_CLOSING) {
                console_puts("run ms ");
                console_dec(display_run_ms());
                console_puts("\n");
                line(3, "LAST RUN MS", (int)display_run_ms());
            }
            state = now;
            state_line(state);
        }
        if (millis() - shown >= 100) {
            int p = display_position(), w = display_open_switch();
            shown = millis();
            if (p != pos) {
                pos = p;
                line(1, "POSITION", p);
            }
            if (w != sw) {
                sw = w;
                line(2, "OPEN SWITCH", w);
            }
        }
        cpu_idle();
    }
}
