/* An MP3 player: the files of a card's top directory, played by the
 * card processor, heard through the unit's amplifier.  The 192.
 *
 *   select knob, NEXT, PREV   another file
 *   SELECT                    stop, and play again
 *   volume knob               louder, quieter
 *   RETURN                    the tuner back as the source, and the
 *                             unit restarts
 *
 * It is the first program of the SDK that uses the card processors'
 * player and the radio processor's amplifier, and both say what they
 * answered on the CAN bus for whoever watches (0x7F8, test 0x30: item
 * 0 the card's slot and how it was reached, 1 the amplifier's answer
 * to the source, 2 each file started and what sd_play said, 3 how a
 * file ended, 4 the volume and the answer).
 *
 * Small enough to come from the card itself (examples/sdboot). */
#include "rnse.h"

#define FILES 12

static char names[FILES][12];
static int files, current, volume = 8, row;

static void line(int y, const char *text, u8 colour)
{
    screen_fill(0, y, screen_width, 16, 0);
    screen_text(8, y, text, colour, 2);
}

static void say(const char *text)
{
    line(24 + 18 * row++, text, 1);
    console_puts(text);
    console_puts("\n");
}

static void rec(u8 item, u32 value, u32 second)
{
    u8 d[8] = { 0x30, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, (u8)(second >> 8), (u8)second };
    can_send_for(0x7f8, d, 8, 3000);
}

static int is_mp3(const char *n)
{
    return n[8] == 'M' && n[9] == 'P' && n[10] == '3';
}

static void show(void)
{
    char text[40], *p;
    int i, j;
    for (i = 0; i < files; i++) {
        p = text;
        *p++ = i == current ? '>' : ' ';
        *p++ = ' ';
        for (j = 0; j < 8 && names[i][j] != ' '; j++)
            *p++ = names[i][j];
        *p = 0;
        line(60 + 18 * i, text, i == current ? 2 : 1);
    }
    p = text;
    for (i = 0; "VOLUME "[i]; i++)
        *p++ = "VOLUME "[i];
    if (volume >= 10)
        *p++ = (char)('0' + udiv((u32)volume, 10));
    *p++ = (char)('0' + umod((u32)volume, 10));
    *p = 0;
    line(screen_height - 20, text, 1);
}

/* What the card processor and the driver last said to each other, when
 * something went wrong: items 0x40 on, two words each, the second
 * value 1 for a word of the host's. */
static void heard(void)
{
    u32 i, n = sd_heard_n < 48 ? sd_heard_n : 48;
    for (i = 0; i < n; i++) {
        u32 at = umod(sd_heard_n - n + i, 48);
        rec((u8)(0x40 + i), sd_heard[at], sd_heard_host[at]);
    }
}

static int failures, amplifier;

static void start(void)
{
    int r = sd_play(names[current]);
    rec(2, (u32)current, (u32)r);
    rec(1, (u32)amplifier, 0);
    if (r) {
        heard();
        failures++;
    }
    console_puts(r ? "not played: " : "playing: ");
    console_puts(names[current]);
    console_puts("\n");
    show();
}

int main(void)
{
    int slot, r = -1, i, was[4] = { 0 };
    u32 size;
    board_init();
    console_puts("\nrnse sdk: player\n");
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 200, 0);
    can_init();
    line(4, "MP3 FROM THE CARD", 2);
    for (slot = 0; slot < 2 && r; slot++) {
        r = sd_attach(slot);
        if (r)
            r = sd_init(slot) ? -1 : 1;
        if (r >= 0)
            rec(0, (u32)slot, (u32)r);
        r = r < 0;
    }
    if (r) {
        say("NO CARD");
        rec(0, 0xffffffff, 0);
    }
    for (i = 0; !r && files < FILES && sd_list(i, names[files], &size) == 0; i++)
        if (is_mp3(names[files]))
            files++;
    if (!r && !files)
        say("NO MP3 FILES ON IT");
    /* The faceplate last: it wants talking to from here on. */
    panel_init();
    if (files) {
        r = radio_source(RADIO_DECODER, volume);
        amplifier = r;
        rec(1, (u32)r, 0);
        if (r)
            say("THE AMPLIFIER DOES NOT ANSWER");
        start();
    }
    for (;;) {
        int key[4], turn;
        panel_poll();
        key[0] = panel_key(KEY_NEXT);
        key[1] = panel_key(KEY_PREV);
        key[2] = panel_key(KEY_SELECT);
        key[3] = panel_key(KEY_RETURN);
        turn = panel_knob(KNOB_SELECT) + (key[0] && !was[0]) - (key[1] && !was[1]);
        if (files && turn) {
            current = (int)umod((u32)(current + files * 8 + turn), (u32)files);
            start();
        }
        if (files && key[2] && !was[2]) {
            sd_play_stop();
            start();
        }
        turn = panel_knob(KNOB_VOLUME);
        if (turn) {
            volume += turn;
            volume = volume < 0 ? 0 : volume > 30 ? 30 : volume;
            rec(4, (u32)volume, (u32)radio_volume(volume));
            show();
        }
        if (key[3] && !was[3]) {
            sd_play_stop();
            radio_source(RADIO_TUNER, 7);
            restart();
        }
        for (i = 0; i < 4; i++)
            was[i] = key[i];
        if (files) {
            r = sd_play_poll();
            if (r <= 0) {
                /* At its end, the next one; after an error what was
                 * said goes out, and after three the player rests. */
                rec(3, (u32)current, (u32)r);
                if (r < 0) {
                    heard();
                    failures++;
                }
                if (failures >= 3) {
                    line(screen_height - 40, "THE PLAYER GAVE UP: SEE THE CAN LOG", 2);
                    files = 0;
                } else {
                    current = (int)umod((u32)(current + 1), (u32)files);
                    start();
                }
            }
        }
    }
}
