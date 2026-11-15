/* Persistent SD test loader for the North American 192 / V222 0371.
 * RNSE.BIN is a rebuilt SDK image. Completion returns here in RAM;
 * remove/reinsert the card, or press RETURN, to run another test.
 * SETUP twice within five seconds starts the carried stock updater. */
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
    u32 changed[2] = { 0, 0 }, size, confirm = 0, keys_start, blocked = 0;
    int raw[2], stable[2] = { 0, 0 }, armed[2] = { 1, 1 };
    int slot, r, status = 0, returned, setup_down = 0, return_down = 0;

    returned = loader_returned(&status, &blocked);
    board_init();
    console_puts("\nrnse sdk: sdboot\n");
    s = screen_width / 400;
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 96, 0);
    screen_text(8 * s, 6 * s, "PROGRAMS FROM AN SD CARD", 2, 2 * s);
    panel_init();
    sd_idle = panel_poll;
    can_init();
    keys_start = millis();
    return_down = returned >= 0;
    if (returned >= 0) {
        armed[0] = !(blocked & 1);
        armed[1] = !(blocked & 2);
        number("TEST RETURNED:", status);
    }
    say("WAITING FOR CARD", 1);
    say("RETURN: RERUN  SETUP TWICE: UPDATE", 2);
    for (slot = 0; slot < 2; slot++) {
        raw[slot] = sd_present(slot);
        changed[slot] = millis();
    }
    for (;;) {
        panel_poll();
        if (panel_key(KEY_SETUP) && !setup_down) {
            if (confirm && millis() - confirm < 5000 && stock_loader_carried())
                stock_update();
            confirm = millis() | 1;
            row = 5;
            say("SETUP AGAIN TO UPDATE STOCK FIRMWARE", 2);
        }
        setup_down = panel_key(KEY_SETUP);
        if (panel_key(KEY_RETURN) && !return_down) {
            armed[0] = armed[1] = 1;
            confirm = 0;
        }
        return_down = panel_key(KEY_RETURN) ||
            (returned >= 0 && (!panel_online() || millis() - keys_start < 1000));
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
                continue;
            }
            if (!armed[slot])
                continue;
            armed[slot] = 0;
            row = 3;
            number("SLOT", slot + 1);
            r = sd_attach(slot);
            if (r) r = sd_init(slot);
            record((u8)slot, (u32)r, 0);
            if (r == -2) {
                say("NEEDS V222 0371 INSTALLED SOFTWARE", 2);
                continue;
            }
            if (!r) r = sd_load("RNSE    BIN", image, 0x1c000, &size);
            record((u8)(2 + slot), (u32)r, 0);
            if (!r && !sd_present(slot)) r = -20;
            if (!r) {
                number("RNSE.BIN, BYTES:", (int)size);
                say("STARTING IT", 2);
                r = loader_run(image, size, slot, (!armed[0]) | (!armed[1] << 1));
            }
            number("LOAD ERROR:", r);
            say("REMOVE / REINSERT CARD OR RETURN", 2);
        }
        cpu_idle();
    }
}
