/* Ways to start a card that will not start, one after the other, each
 * on a card that has just gone in (192).  Run it from a card through
 * the SD loader, take that card out, and put the card to try into
 * either slot when the screen says so; take it out again after each
 * line.  Each line is one way: OK when the card started and its first
 * sector reads, or what the start answered (the result, the command it
 * stopped at, the processor's answer).  The same goes out on CAN, 0x7f8
 * with 0x30 first: way, result, command and answer.  RETURN held goes
 * back to the loader, at the end or at any time.
 *
 * The ways: the stock driver's start (sd_restart, as the loader has
 * it); the same with nothing else done while it waits (no faceplate,
 * the answers read at once); 10 ms between steps instead of 150; the
 * whole voltage window in ACMD41; ACMD41 every 10 ms without the
 * second, CMD55-less one; the processor reset first (sd_init); and the
 * stock start a second after the card went in. */
#include "rnse.h"

static int row;

static void line(const char *text, u8 colour)
{
    screen_fill(0, 28 + 18 * row, screen_width, 18, 0);
    screen_text(8, 30 + 18 * row, text, colour, 2);
    console_puts(text);
    console_puts("\n");
    row++;
}

static char *put_hex(char *p, u32 v, int digits)
{
    while (digits--)
        *p++ = "0123456789ABCDEF"[(v >> (4 * digits)) & 15];
    return p;
}

static char *put_text(char *p, const char *t)
{
    while (*t)
        *p++ = *t++;
    return p;
}

static void wait_ms(u32 ms)
{
    u32 t = millis();
    while (millis() - t < ms) {
        panel_service();
        cpu_idle();
    }
}

/* A card going in, in either slot: which slot. */
static int wait_for_card(void)
{
    int was[2], slot;
    for (slot = 0; slot < 2; slot++)
        was[slot] = sd_present(slot);
    for (;;) {
        for (slot = 0; slot < 2; slot++) {
            int now = sd_present(slot);
            if (now && !was[slot]) {
                wait_ms(150);
                if (sd_present(slot))
                    return slot;
            }
            was[slot] = now;
        }
        panel_service();
        cpu_idle();
    }
}

static void wait_for_removal(int slot)
{
    while (sd_present(slot)) {
        panel_service();
        cpu_idle();
    }
    wait_ms(150);
    sd_card_out(slot);
}

static const char *const name[] = {
    "STOCK", "NO WAIT WORK", "10 MS STEPS", "FULL WINDOW", "PLAIN FAST", "PROCESSOR RESET", "1 S LATER",
};
#define WAYS 7

int main(void)
{
    static u8 sector[512];
    char text[48], *p;
    int way, slot, r;
    u8 d[8];

    board_init();
    console_puts("rnse sdk: cardstart\n");
    screen_palette(0, 0, 0, 0);
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 96, 0);
    screen_fill(0, 0, screen_width, screen_height, 0);
    screen_text(8, 6, "WAYS TO START A CARD", 2, 2);
    panel_attach();
    sd_idle = panel_service;
    display_idle = panel_service;
    can_init();
    row = 0;
    line("TAKE THIS CARD OUT. THEN, EACH", 1);
    line("TIME: CARD IN, WAIT, CARD OUT", 1);
    for (way = 0; way < WAYS; way++) {
        row = 2 + way;
        p = put_text(text, "PUT IN: ");
        p = put_text(p, name[way]);
        *p = 0;
        line(text, 2);
        row--;
        slot = wait_for_card();
        p = put_text(text, "STARTING: ");
        p = put_text(p, name[way]);
        *p = 0;
        line(text, 2);
        row--;
        sd_pace = 150;
        sd_window = 0x00ff0000;
        sd_no_second_acmd41 = 0;
        if (way == 1)
            sd_idle = 0;
        if (way == 2)
            sd_pace = 10;
        if (way == 3)
            sd_window = 0x00ff8000;
        if (way == 4) {
            sd_pace = 10;
            sd_no_second_acmd41 = 1;
        }
        if (way == 6)
            wait_ms(1000);
        r = way == 5 ? sd_init(slot) : sd_restart(slot);
        sd_idle = panel_service;
        sd_pace = 150;
        sd_window = 0x00ff0000;
        sd_no_second_acmd41 = 0;
        p = text;
        *p++ = (char)('1' + way);
        *p++ = ' ';
        if (r == 0) {
            r = sd_read(0, sector);
            if (r == 0 && (sector[510] != 0x55 || sector[511] != 0xaa))
                r = -99;
            p = put_text(p, r == 0 ? "OK" : "STARTED, NO READ");
        } else {
            *p++ = '-';
            *p++ = (char)('0' + (-r) / 10 % 10);
            *p++ = (char)('0' + (-r) % 10);
            *p++ = ' ';
            p = put_hex(p, sd_last[0], 4);
            *p++ = ' ';
            p = put_hex(p, sd_last[1], 4);
            *p++ = ' ';
            p = put_hex(p, sd_last[2], 4);
        }
        *p = 0;
        line(text, r == 0 ? 1 : 2);
        d[0] = 0x30;
        d[1] = (u8)way;
        d[2] = (u8)r;
        d[3] = (u8)slot;
        d[4] = (u8)(sd_last[0] >> 8);
        d[5] = (u8)sd_last[0];
        d[6] = (u8)(sd_last[1] >> 8);
        d[7] = (u8)sd_last[1];
        can_send_for(0x7f8, d, 8, 3000);
        if (way + 1 < WAYS) {
            row = 2 + way + 1;
            line("TAKE IT OUT", 2);
            wait_for_removal(slot);
        }
    }
    row = 2 + WAYS;
    line("DONE. HOLD RETURN: LOADER", 2);
    for (;;) {
        panel_service();
        cpu_idle();
    }
}
