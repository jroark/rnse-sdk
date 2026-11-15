/* What the CD/DVD drive says about itself while a disc goes out and
 * comes in: a log, with times, of every change in its answer to "are
 * you ready".
 *
 * The emulator has to assume how the drive behaves when a disc is put
 * in: what it says while it loads the disc and for how long, and
 * whether "the medium may have changed" comes before that or after.
 * The stock software takes a disc that goes in while it runs for an
 * update only if those answers come as it expects (docs/boot-rom.md,
 * "A disc that goes in while the unit runs"); the emulator's drive
 * says what suits all the software seen, which is an inference.
 *
 * The program runs from memory, so the disc it came on can leave:
 *
 *   CD EJECT   the drive is told to push the disc out
 *              (take it, push it in again; or try another disc)
 *   SETUP      done: with the update disc in the drive, hand over to
 *              the stock loader, which makes the unit whole again
 *   RETURN     done, without an update: the unit starts again as it
 *              is (and, with the disc in the drive, the program after
 *              it: see restart in rnse.h)
 *
 * Every 100 ms the drive is asked TEST UNIT READY and, if it is not,
 * REQUEST SENSE.  A line is written for every change: the time in
 * milliseconds since the program started, and READY or the three
 * numbers of the sense data (key/code/qualifier, e.g. 2/3A/00 no disc,
 * 2/04/00 not ready, 2/04/01 becoming ready, 6/28/00 the medium may have
 * changed).  The
 * lines go to the screen (photograph it), to the debug serial port and
 * to the CAN bus:
 *
 *   7F5  line number, time (3 bytes, ms), key, code, qualifier,
 *        0 = error / 1 = ready / 2 = the drive did not answer
 *
 * EXTRA_CFLAGS: -DMINUTES=n ends it by itself after n minutes (default
 * 10), for a unit whose keys do not answer: with the stock update, or,
 * built with -DRESTART, with a restart. */
#include "rnse.h"

#ifndef MINUTES
#define MINUTES 10
#endif

static const u8 test_unit_ready[12] = { 0x00 };
static const u8 request_sense[12] = { 0x03, 0, 0, 0, 18 };
static const u8 allow_removal[12] = { 0x1e };
static const u8 eject[12] = { 0x1b, 1, 0, 0, 2 };

static int s;
#define ROWS 11
static int lines;

static char *put_hex(char *p, u32 v, int digits)
{
    while (digits--)
        *p++ = "0123456789ABCDEF"[v >> (4 * digits) & 15];
    return p;
}

static char *put_dec(char *p, u32 v, int digits)
{
    int i;
    for (i = digits - 1; i >= 0; i--) {
        p[i] = (char)('0' + umod(v, 10));
        v = udiv(v, 10);
    }
    return p + digits;
}

static void show(int row, const char *text, u8 colour)
{
    screen_fill(4 * s, (22 + 16 * row) * s, screen_width - 8 * s, 14 * s, 0);
    screen_text(4 * s, (22 + 16 * row) * s, text, colour, 2 * s);
}

/* One line of the log, everywhere it goes. */
static void log_line(u32 ms, int kind, u8 key, u8 code, u8 qualifier)
{
    char text[32], *p = text;
    u8 d[8];
    p = put_dec(p, ms, 7);
    *p++ = ' ';
    if (kind == 1) {
        const char *q = "READY";
        while (*q)
            *p++ = *q++;
    } else if (kind == 2) {
        const char *q = "NO ANSWER";
        while (*q)
            *p++ = *q++;
    } else {
        p = put_hex(p, key, 1);
        *p++ = '/';
        p = put_hex(p, code, 2);
        *p++ = '/';
        p = put_hex(p, qualifier, 2);
    }
    *p = 0;
    show(lines % ROWS, text, 1);
    /* The line after it is cleared: that is where the log is now. */
    show((lines + 1) % ROWS, "", 1);
    console_puts(text);
    console_puts("\n");
    d[0] = (u8)lines;
    d[1] = (u8)(ms >> 16);
    d[2] = (u8)(ms >> 8);
    d[3] = (u8)ms;
    d[4] = key;
    d[5] = code;
    d[6] = qualifier;
    d[7] = (u8)kind;
    can_send(0x7f5, d, 8);
    lines++;
}

int main(void)
{
    u32 start, next = 0, last = 0xffffffff;
    int was_eject = 0, again = 0;

    board_init();
    console_puts("\nrnse sdk: drive\n");
    s = screen_width / 400;
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 96, 0);
    screen_text(4 * s, 2 * s, "DRIVE LOG  CD EJECT: OUT  SETUP: UPDATE  RETURN: RESTART", 2, s);
    panel_init();
    can_init();
    cd_init();
    cd_command(allow_removal, 0, 0);
    start = millis();

    for (;;) {
        u32 now = millis() - start;
        panel_poll();
        if (panel_key(KEY_CD_EJECT) && !was_eject) {
            console_puts("eject\n");
            cd_command(eject, 0, 0);
        }
        was_eject = panel_key(KEY_CD_EJECT);
        if (panel_key(KEY_RETURN))
            again = 1;
#ifdef RESTART
        if (now > MINUTES * 60000u)
            again = 1;
#endif
        if (again || panel_key(KEY_SETUP) || now > MINUTES * 60000u)
            break;
        if ((s32)(now - next) >= 0) {
            u8 sense[18];
            u32 state;
            int r = cd_command(test_unit_ready, 0, 0), kind;
            next = now + 100;
            if (r == 0) {
                kind = 1;
                sense[2] = sense[12] = sense[13] = 0;
            } else if (r < 0 || cd_command(request_sense, sense, sizeof sense)) {
                kind = 2;
                sense[2] = sense[12] = sense[13] = 0;
            } else {
                kind = 0;
            }
            state = (u32)kind << 24 | (u32)(sense[2] & 15) << 16 | (u32)sense[12] << 8 | sense[13];
            if (state != last) {
                last = state;
                log_line(now, kind, sense[2] & 15, sense[12], sense[13]);
            }
        }
        cpu_idle();
    }

    show(ROWS, "", 1);
    if (again) {
        show(ROWS, "RESTARTING", 2);
        console_puts("restarting\n");
        delay_ms(1000);
        restart();
    }
    if (!stock_loader_carried()) {
        show(ROWS, "NO STOCK LOADER: NO WAY BACK", 2);
        for (;;)
            cpu_idle();
    }
    show(ROWS, "STARTING THE STOCK LOADER", 2);
    console_puts("starting the stock loader\n");
    stock_update();
    show(ROWS, "THE STOCK LOADER DID NOT UNPACK", 2);
    for (;;)
        cpu_idle();
}
