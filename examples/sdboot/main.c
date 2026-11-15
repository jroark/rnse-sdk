/* Programs from an SD card.  This one goes on a disc, once; what it
 * starts is the file RNSE.BIN in the root directory of a FAT card in
 * either slot: a program built with build.sh, the .bin as it is.  A new
 * program to try is then a file copied to the card, not a disc burned.
 *
 * It looks in the first slot and then in the second, says on the screen
 * what it found, and starts the program in its own place, with the
 * packed stock loader still there for the program's stock_update.  A
 * program that ends in restart() is back here half a minute later,
 * with whatever is on the card by then.
 *
 * Without a card or a file it says so and waits: SETUP hands over to
 * the stock loader (the unit updates itself from the disc and the disc
 * is no update any more), RETURN or a minute restarts the unit, and so
 * comes back here.
 *
 * What it found also goes out on the CAN bus, for a logic analyser
 * (frames 0x7F8, as the suite's: 0x20, the slot, then what sd_init
 * said and what sd_load said; 0x21 with the file's size when it starts
 * one).
 *
 * The 192 with the North American software 0371 only: see src/sd.c. */
#include "rnse.h"

static int s, row;

static void say(const char *text, u8 colour)
{
    screen_fill(0, (30 + 14 * row) * s, screen_width, 12 * s, 0);
    screen_text(8 * s, (30 + 14 * row) * s, text, colour, 2 * s);
    console_puts(text);
    console_puts("\n");
    row++;
}

static void record(u8 item, u32 value, u32 second)
{
    u8 d[8] = { 0x20, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, (u8)(second >> 8), (u8)second };
    can_send_for(0x7f8, d, 8, 3000);
}

static void number(const char *what, int v)
{
    char text[40], *p = text, t[12];
    int n = 0;
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
    say(text, 1);
}

int main(void)
{
    u8 *image = (u8 *)(way_back_scratch + 0x1000);
    u32 size = 0, start;
    int slot, r = -1;

    board_init();
    console_puts("\nrnse sdk: sdboot\n");
    s = screen_width / 400;
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 96, 0);
    screen_text(8 * s, 6 * s, "PROGRAMS FROM AN SD CARD", 2, 2 * s);
    /* The faceplate controller is left as the installed software has
     * it, lighting and all, for the program that comes: started anew
     * here and then not talked to while the card is read, it would
     * have no settings and the screen no light (which is what a unit
     * showed).  It is started below, when its keys are wanted. */
    can_init();
    for (slot = 0; slot < 2 && r; slot++) {
        u32 t = millis();
        number("SLOT", slot + 1);
        r = sd_init(slot);
        record((u8)slot, (u32)r, millis() - t);
        record((u8)(8 + slot), (u32)sd_last[0] << 16 | sd_last[1], sd_last[2]);
        if (r) {
            number("  NO CARD, OR NOT STARTED:", r);
            number("    AT COMMAND", sd_last[0] & 0x3f);
            number("    ANSWER", sd_last[1]);
            continue;
        }
        t = millis();
        r = sd_load("RNSE    BIN", image, 0x100000, &size);
        if (r == 0 && size > 0x1c000)
            r = -3; /* more than the 192's place for a program holds */
        record((u8)(2 + slot), (u32)r, millis() - t);
        if (r) {
            number("  NO RNSE.BIN:", r);
            number("    AT COMMAND", sd_last[0] & 0x3f);
            number("    ANSWER", sd_last[1]);
        }
    }
    if (r == 0) {
        record(0x21, size, 0);
        number("RNSE.BIN, BYTES:", (int)size);
        say("STARTING IT", 2);
        delay_ms(500);
        r = run_image(image, size);
        number("NOT A PROGRAM FOR THIS PLACE:", r);
    }
    say("SETUP: STOCK UPDATE  RETURN: RESTART", 2);
    panel_init();
    start = millis();
    for (;;) {
        panel_poll();
        if (panel_key(KEY_SETUP) && stock_loader_carried())
            stock_update();
        if (panel_key(KEY_RETURN) || millis() - start > 60000)
            restart();
        cpu_idle();
    }
}
