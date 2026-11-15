/* Persistent SD test loader for the North American 192 / V222 0371.
 *
 * The programs are the files NAME.BIN in the card's top directory,
 * rebuilt SDK images (docs/sd-loader.md).  With one, it runs when the
 * card goes in; with several, a menu: the select knob, NEXT and PREV
 * choose, SELECT runs.  A program that returns comes back here in RAM,
 * to the menu with that program chosen; RETURN runs it again.  Taking
 * the card out and putting it back starts over.  RETURN held for two
 * seconds goes back to the stock software: the display opens, the disc
 * is ejected, and the unit starts anew (no flash is written).
 *
 * A loader on the card as RNSE.BIN is left out of the menu: that is how
 * this loader comes to a unit whose disc carries an older one, which
 * runs RNSE.BIN; this one then stays, as the loader programs return to,
 * until the unit is switched off.
 *
 * The key next to the power button opens and closes the display, CD
 * EJECT behind it ejects the disc, wherever the loader waits.  There
 * is deliberately no way from here to the stock updater or any other
 * writing of the flash (CLAUDE.md, "Mandatory hardware behavior"). */
#include "rnse.h"

/* (Nine rows, above the line about the display key.) */
#define MAX_FILES 9
#define MENU_ROW 4
/* loader_note's flag: the program was started from the menu. */
#define FROM_MENU 1

static int s, row;
static char names[MAX_FILES][12];
static int files, chosen, menu_slot = -1;

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

/* "PCMTEST BIN" as "PCMTEST.BIN". */
static void file_name(char *out, const char *name)
{
    int i;
    for (i = 0; i < 8 && name[i] != ' '; i++)
        *out++ = name[i];
    *out++ = '.';
    for (i = 8; i < 11 && name[i] != ' '; i++)
        *out++ = name[i];
    *out = 0;
}

static int same(const char *a, const char *b)
{
    int i;
    for (i = 0; i < 11; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

/* Whether the image holds a loader's banner (every version of this
 * one prints it). */
static int is_loader(const u8 *image, u32 size)
{
    static const char mark[] = "rnse sdk: sdboot";
    u32 i, k;
    for (i = 0; i + sizeof mark - 1 <= size; i++) {
        for (k = 0; k < sizeof mark - 1 && image[i + k] == (u8)mark[k]; k++)
            ;
        if (k == sizeof mark - 1)
            return 1;
    }
    return 0;
}

/* The programs on the card, in directory order. */
static void list(u8 *image)
{
    char name[12];
    u32 size, i;
    files = 0;
    for (i = 0; files < MAX_FILES && sd_list((int)i, name, &size) == 0; i++) {
        if (name[8] != 'B' || name[9] != 'I' || name[10] != 'N' || name[0] == '_')
            continue;
        if (same(name, "RNSE    BIN") && sd_load(name, image, 0x1c000, &size) == 0 && is_loader(image, size))
            continue;
        for (size = 0; size < 12; size++)
            names[files][size] = name[size];
        files++;
    }
}

static void draw_menu(void)
{
    char text[20];
    int i;
    row = MENU_ROW - 1;
    say("KNOB: CHOOSE  SELECT: RUN", 2);
    for (i = 0; i < MAX_FILES; i++) {
        row = MENU_ROW + i;
        if (i >= files) {
            screen_fill(0, (30 + 14 * row) * s, screen_width, 12 * s, 0);
            continue;
        }
        text[0] = i == chosen ? '>' : ' ';
        text[1] = ' ';
        file_name(text + 2, names[i]);
        say(text, i == chosen ? 2 : 1);
    }
}

static void clear_menu(void)
{
    int i;
    for (i = MENU_ROW - 1; i < MENU_ROW + MAX_FILES; i++)
        screen_fill(0, (30 + 14 * i) * s, screen_width, 12 * s, 0);
    menu_slot = -1;
}

/* Load a program of the card in `slot` and start it; returns only when
 * that failed (the reason, below 0). */
static int run(int slot, const char *name, u8 *image, u32 blocked, u32 flags)
{
    char text[24];
    u32 size;
    int r = sd_load(name, image, 0x1c000, &size);
    record((u8)(2 + slot), (u32)r, 0);
    if (!r && !sd_present(slot))
        r = -20;
    if (r)
        return r;
    clear_menu();
    row = 2;
    file_name(text, name);
    say(text, 1);
    number("BYTES:", (int)size);
    say("STARTING IT", 2);
    loader_note(name, flags);
    /* A display on its way stops where it is at the hand-over; let it
     * get there first. */
    display_wait(10000);
    return loader_run(image, size, slot, blocked);
}

/* A card to start from: its card processor, and the programs on it.
 * Returns how many there are, or the error. */
static int open_card(int slot, u8 *image)
{
    int r;
    row = 2;
    number("SLOT", slot + 1);
    r = sd_attach(slot);
    if (r)
        r = sd_init(slot);
    record((u8)slot, (u32)r, 0);
    if (r == -2) {
        say("NEEDS V222 0371 INSTALLED SOFTWARE", 2);
        return r;
    }
    if (r)
        return r;
    list(image);
    if (!files) {
        say("NO .BIN PROGRAMS ON THE CARD", 2);
        return -2;
    }
    return files;
}

static void show_menu(int slot)
{
    char last[12];
    int i;
    menu_slot = slot;
    chosen = 0;
    if (loader_noted(last, 0))
        for (i = 0; i < files; i++)
            if (same(names[i], last))
                chosen = i;
    draw_menu();
}

/* RETURN held for two seconds: back to the stock software.  The disc
 * would start this loader again, so it goes out first: the display is
 * opened (it hides the drive), and only when it is fully open is the
 * disc ejected.  Then the unit is started anew, by its own start code:
 * nothing is written to the flash. */
static void to_stock(void)
{
    u32 t;
    clear_menu();
    row = 2;
    say("BACK TO THE STOCK SOFTWARE", 2);
    say("OPENING THE DISPLAY", 1);
    display_open();
    if (display_wait(10000) == DISPLAY_OPEN) {
        say("EJECTING THE DISC: TAKE IT OUT", 1);
        cd_eject();
    } else {
        say("THE DISPLAY DID NOT OPEN: DISC LEFT IN", 2);
    }
    /* Long enough to read, the keys answered meanwhile. */
    t = millis();
    while (millis() - t < 3000) {
        panel_service();
        cpu_idle();
    }
    restart();
}

int main(void)
{
    u8 *image = (u8 *)(way_back_scratch + 0x1000);
    u32 changed[2] = { 0, 0 }, keys_start, blocked = 0;
    int raw[2], stable[2] = { 0, 0 }, armed[2] = { 1, 1 };
    int slot, r, status = 0, returned, return_armed = 0;
    int next_down = 0, prev_down = 0, select_down = 0;
    char last[12];
    u32 flags = 0;

    returned = loader_returned(&status, &blocked);
    board_init();
    console_puts("\nrnse sdk: sdboot\n");
    s = screen_width / 400;
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 96, 0);
    screen_text(8 * s, 6 * s, "PROGRAMS FROM AN SD CARD", 2, 2 * s);
    panel_init();
    sd_idle = panel_service;
    display_idle = panel_service;
    can_init();
    /* RETURN held is this loader's own: back to the stock software
     * (below), not to a loader. */
    panel_return_to_loader = 0;
    keys_start = millis();
    if (returned >= 0) {
        armed[0] = !(blocked & 1);
        armed[1] = !(blocked & 2);
        if (status == LOADER_RETURN_HELD)
            say("TEST ENDED BY HOLDING RETURN", 1);
        else
            number("TEST RETURNED:", status);
    }
    say("WAITING FOR CARD", 1);
    say("RETURN: RERUN.  HOLD 2 S: STOCK", 2);
    screen_text(8 * s, screen_height - 12 * s, "KEY BY THE POWER BUTTON: DISPLAY.  CD EJECT: DISC OUT", 2, s);
    for (slot = 0; slot < 2; slot++) {
        raw[slot] = sd_present(slot);
        changed[slot] = millis();
    }
    /* Back from a program of a card with several: the menu again, for
     * the same card, with that program chosen. */
    if (returned >= 0 && loader_noted(last, &flags) && (flags & FROM_MENU) &&
        sd_present(returned) && open_card(returned, image) > 1)
        show_menu(returned);
    for (;;) {
        int knob;
        panel_service();
        /* RETURN counts once the faceplate answers, a second after the
         * start, and once it has been seen let go: a RETURN still held
         * from the program that just ended is not a new one. */
        if (!return_armed && panel_online() && millis() - keys_start >= 1000 && !panel_key(KEY_RETURN)) {
            return_armed = 1;
            (void)panel_return_short();
            (void)panel_return_held();
        }
        if (return_armed && panel_return_held())
            to_stock();
        if (return_armed && panel_return_short()) {
            /* The program run last, from the menu's card; else every
             * card that is in, as if it had just gone in. */
            if (menu_slot >= 0 && loader_noted(last, 0)) {
                r = run(menu_slot, last, image, (!armed[0]) | (!armed[1] << 1), FROM_MENU);
                number("LOAD ERROR:", r);
            } else {
                armed[0] = armed[1] = 1;
            }
        }

        /* (Read every round, so that turns made with no menu up do
         * not wait for one.) */
        knob = panel_knob(KNOB_SELECT);
        if (menu_slot >= 0) {
            if (panel_key(KEY_NEXT) && !next_down)
                knob++;
            if (panel_key(KEY_PREV) && !prev_down)
                knob--;
            if (knob) {
                chosen = (int)umod((u32)(chosen + knob + 8 * files), (u32)files);
                draw_menu();
            }
            if (panel_key(KEY_SELECT) && !select_down) {
                r = run(menu_slot, names[chosen], image, (!armed[0]) | (!armed[1] << 1), FROM_MENU);
                number("LOAD ERROR:", r);
                draw_menu();
            }
        }
        next_down = panel_key(KEY_NEXT);
        prev_down = panel_key(KEY_PREV);
        select_down = panel_key(KEY_SELECT);

        for (slot = 0; slot < 2; slot++) {
            int present = sd_present(slot);
            if (raw[slot] != present) {
                raw[slot] = present;
                changed[slot] = millis();
            }
            if (millis() - changed[slot] < 100)
                continue;
            stable[slot] = present;
            if (!stable[slot]) {
                armed[slot] = 1;
                if (menu_slot == slot)
                    clear_menu();
                continue;
            }
            if (!armed[slot])
                continue;
            armed[slot] = 0;
            clear_menu();
            r = open_card(slot, image);
            if (r == 1) {
                r = run(slot, names[0], image, (!armed[0]) | (!armed[1] << 1), 0);
            } else if (r > 1) {
                show_menu(slot);
                continue;
            }
            number("LOAD ERROR:", r);
            say("REMOVE / REINSERT CARD OR RETURN", 2);
        }
        cpu_idle();
    }
}
