/* The other loader of a firmware disc (`rnse firmware --swap`).
 *
 * A disc has its loader twice: in the container, where the installed
 * software finds it for an update, and as a file of its own, which is
 * the one the boot ROM runs when the unit has no software.  On a
 * firmware disc the first is the stock loader, which installs the
 * program.  If the second were too, a unit whose program has been
 * taken out (or has taken itself out) with that disc still in the
 * drive would get the program straight back, and a program that does
 * not work would never let the disc out.
 *
 * So the second is this: it pushes the disc out, says what to put in,
 * and when a disc has gone in and is ready it restarts the unit, for
 * the boot ROM to run whatever loader that disc has.  CD EJECT pushes
 * a disc out again.  It does nothing else, on purpose. */
#include "rnse.h"

static const u8 allow_removal[12] = { 0x1e, 0, 0, 0, 0 };
static const u8 eject[12] = { 0x1b, 1, 0, 0, 2 };
static const u8 test_unit_ready[12] = { 0x00 };

static void line(int y, const char *text, u8 colour)
{
    screen_fill(0, y, screen_width, 16, 0);
    screen_text(8, y, text, colour, 2);
}

int main(void)
{
    int gone = 0, was = 0;
    u32 next = 0;
    board_init();
    console_puts("\nrnse sdk: swap\n");
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 200, 0);
    line(8, "NO SOFTWARE IN THE UNIT", 2);
    line(50, "THIS DISC HOLDS A PROGRAM.", 1);
    line(70, "PUT THE STOCK UPDATE DISC IN:", 1);
    line(90, "IT IS INSTALLED BY ITSELF.", 1);
    line(130, "CD EJECT: THE DISC OUT AGAIN", 1);
    panel_init();
    cd_init();
    cd_command(allow_removal, 0, 0);
    cd_command(eject, 0, 0);
    console_puts("ejected\n");
    for (;;) {
        panel_poll();
        if (panel_key(KEY_CD_EJECT) && !was) {
            cd_command(eject, 0, 0);
            gone = 0;
        }
        was = panel_key(KEY_CD_EJECT);
        if (millis() < next)
            continue;
        next = millis() + 500;
        if (!gone) {
            /* Until the drive says that it has no disc that is ready. */
            if (cd_command(test_unit_ready, 0, 0) != 0) {
                gone = 1;
                line(170, "WAITING FOR A DISC", 2);
            }
        } else if (cd_wait_ready(0) == 0) {
            line(170, "A DISC: STARTING AGAIN", 2);
            console_puts("a disc is ready, restarting\n");
            delay_ms(1000);
            restart();
        }
    }
}
