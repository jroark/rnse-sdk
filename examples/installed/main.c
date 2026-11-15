/* A program that lives in the unit: installed in the operating
 * system's place in flash (`rnse firmware`), started by the boot ROM
 * at every start, with no disc and no card.  The 192.
 *
 * It shows that it runs, counts the seconds, and shows the keys.  And
 * it has the way out:
 *
 *   CD EJECT                       the disc out (the one that
 *                                  installed the program should not
 *                                  stay in)
 *   RETURN, held for two seconds   the program takes itself out: it
 *                                  erases the directory of the
 *                                  installed software and restarts.
 *                                  The boot ROM then installs whatever
 *                                  update disc is in the drive.
 *
 * Put the stock update disc in before that: with no disc the unit
 * waits for one.  docs/custom-firmware.md, "A program in flash". */
#include "rnse.h"

static const u8 allow_removal[12] = { 0x1e, 0, 0, 0, 0 };
static const u8 eject[12] = { 0x1b, 1, 0, 0, 2 };

static void line(int y, const char *text, u8 colour, int scale)
{
    screen_fill(0, y, screen_width, 8 * scale, 0);
    screen_text(8, y, text, colour, scale);
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

int main(void)
{
    u32 shown = 0xffffffff, held = 0;
    int was_eject = 0;
#ifdef HANG
    /* For the emulator only: a program that is installed and does not
     * run, to see what the boot ROM does about one.  Never on a unit. */
    for (;;)
        ;
#endif
    board_init();
    console_puts("\nrnse sdk: installed\n");
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 200, 0);
    screen_palette(3, 200, 40, 0);
    line(8, "A PROGRAM IN FLASH", 2, 2);
    line(150, "CD EJECT: THE DISC OUT", 1, 2);
    line(170, "HOLD RETURN 2 S: PROGRAM OUT", 1, 2);
    line(190, "STOCK DISC IN THE DRIVE FIRST", 1, 2);
    can_init();
    panel_init();
    cd_init();
    cd_command(allow_removal, 0, 0);
    for (;;) {
        u32 now = udiv(millis(), 1000);
        panel_poll();
        if (now == 10)
            firmware_healthy();
        if (panel_key(KEY_CD_EJECT) && !was_eject) {
            console_puts("eject\n");
            cd_command(eject, 0, 0);
        }
        was_eject = panel_key(KEY_CD_EJECT);
        if (now != shown) {
            char text[32], *p = text;
            shown = now;
            p = put_dec(p, now);
            *p++ = ' ';
            *p++ = 'S';
            *p = 0;
            line(50, text, 1, 4);
            line(100, panel_online() ? "FACEPLATE: ANSWERS" : "FACEPLATE: SILENT", 1, 2);
            line(120, !firmware_installed() ? "NOT FROM FLASH" : now < 10 ? "COUNTED AS A START THAT FAILED" : "COUNTED AS RUNNING", 1, 2);
        }
        if (!panel_key(KEY_RETURN)) {
            held = 0;
        } else if (!held) {
            held = millis() | 1;
        } else if (millis() - held > 2000 && millis() >= held) {
            int r;
            line(210, "TAKING THE PROGRAM OUT", 3, 2);
            console_puts("removing\n");
            r = firmware_remove();
            if (r) {
                line(210, "THE FLASH DID NOT ERASE", 3, 2);
                console_puts("not erased\n");
                held = 0;
            } else {
                console_puts("removed, restarting\n");
                delay_ms(500);
                restart();
            }
        }
    }
}
