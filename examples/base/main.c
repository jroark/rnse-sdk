/* A firmware of one's own, as far as the SDK goes: installed in flash
 * (`rnse firmware`), it starts with the unit and is a radio and an MP3
 * player.  The 192.
 *
 *   RADIO          the tuner; the select knob tunes (200 kHz a detent),
 *                  NEXT and PREV go 1 MHz
 *   MEDIA          MP3 files from a card (first slot, then second);
 *                  the select knob, NEXT and PREV choose
 *   volume knob    louder, quieter
 *   CD EJECT       the disc out
 *   RETURN, 2 s    the program takes itself out of the flash and the
 *                  boot ROM installs the update disc it is given
 *
 * It brings up by itself what the stock software would have: the radio
 * processor (radio_start), the card processors (sd_init, when MEDIA is
 * first pressed).  What each said goes out on the CAN bus (0x7F8, test
 * 0x33): item 0 radio_start's unanswered frames, 7 its log of each
 * frame, 1 the source (with the last refusal and their count) and the
 * answer, 2 the card's slot and sd_init's result with the last card
 * command, 3 a file started, 4 a file's end, 5 a frequency, 6 the
 * volume.
 *
 * Also runs from a card (examples/sdboot), where it finds everything
 * as the stock software left it. */
#include "rnse.h"

#define FILES 10

static const u8 allow_removal[12] = { 0x1e, 0, 0, 0, 0 };
static const u8 eject[12] = { 0x1b, 1, 0, 0, 2 };
static char names[FILES][12];
static int files, current, volume = 8, frequency = 2042, media, card = -2;

static void line(int y, const char *text, u8 colour, int scale)
{
    screen_fill(0, y, screen_width, 8 * scale, 0);
    screen_text(8, y, text, colour, scale);
}

static void rec(u8 item, u32 value, u32 second)
{
    u8 d[8] = { 0x33, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, (u8)(second >> 8), (u8)second };
    can_send_for(0x7f8, d, 8, 3000);
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

static char *put_str(char *p, const char *q)
{
    while (*q)
        *p++ = *q++;
    return p;
}

static void show(const char *note)
{
    char text[40], *p = text;
    int i;
    line(8, media ? "MP3 FROM THE CARD" : "RADIO", 2, 2);
    if (media) {
        if (files) {
            for (i = 0; i < 8 && names[current][i] != ' '; i++)
                *p++ = names[current][i];
        } else {
            p = put_str(p, card == -2 ? "..." : "NO FILES");
        }
    } else {
        u32 khz = (u32)frequency * 50;
        p = put_dec(p, udiv(khz, 1000));
        *p++ = '.';
        p = put_dec(p, udiv(umod(khz, 1000), 100));
        p = put_str(p, " MHZ");
    }
    *p = 0;
    line(50, text, 1, 4);
    p = put_str(text, "VOLUME ");
    p = put_dec(p, (u32)volume);
    *p = 0;
    line(100, text, 1, 2);
    if (note)
        line(124, note, 1, 2);
}

static void play(void)
{
    int r = sd_play(names[current]);
    rec(3, (u32)current, (u32)r);
    console_puts(r ? "not played\n" : "playing\n");
    show(r ? "NOT PLAYED" : "");
}

/* The card, the first time it is wanted. */
static void find_card(void)
{
    int slot, r = -1, i;
    u32 size;
    show("STARTING THE CARD");
    for (slot = 0; slot < 2 && r; slot++) {
        r = sd_attach(slot);
        if (r)
            r = sd_init(slot);
        rec(2, (u32)slot << 16 | (u32)(r & 0xffff), (u32)(sd_last[0] & 0x3f) << 8 | (sd_last[1] >> 8));
    }
    card = r ? -1 : slot - 1;
    for (i = 0; !r && files < FILES && sd_list(i, names[files], &size) == 0; i++)
        if (names[files][8] == 'M' && names[files][9] == 'P' && names[files][10] == '3')
            files++;
    console_puts(r ? "no card\n" : "card\n");
}

static void source(int to_media)
{
    int r;
    if (to_media && card == -2)
        find_card();
    if (!to_media)
        sd_play_stop();
    media = to_media && files;
    r = radio_source(media ? RADIO_DECODER : RADIO_TUNER, volume);
    rec(1, (u32)media << 16 | (u32)(radio_refusal & 0xff) << 8 | (u32)(radio_refusals & 0xff), (u32)r);
    if (media)
        play();
    else
        show(to_media ? (card < 0 ? "NO CARD" : "NO MP3 FILES ON THE CARD") : r ? "THE RADIO DOES NOT ANSWER" : "");
}

int main(void)
{
    u32 held = 0, healthy = 0;
    int was[6] = { 0 }, i, r;
    board_init();
    console_puts("\nrnse sdk: base\n");
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 200, 0);
    screen_palette(3, 200, 40, 0);
    line(screen_height - 40, "RADIO  MEDIA  CD EJECT", 1, 2);
    line(screen_height - 20, "HOLD RETURN 2 S: PROGRAM OUT", 1, 2);
    can_init();
    panel_init();
    sd_idle = panel_poll;
    cd_init();
    cd_command(allow_removal, 0, 0);
    r = radio_start();
    rec(0, (u32)r, (u32)firmware_installed());
    for (i = 0; i < 19; i++)
        rec(7, (u32)i, radio_start_log[i]);
    console_puts(r ? "radio: frames unanswered\n" : "radio started\n");
    source(0);
    for (;;) {
        static const int keys[6] = { KEY_RADIO, KEY_MEDIA, KEY_NEXT, KEY_PREV, KEY_CD_EJECT, KEY_RETURN };
        int key[6], turn, pressed[6];
        panel_poll();
        if (!healthy && millis() > 10000) {
            healthy = 1;
            firmware_healthy();
        }
        for (i = 0; i < 6; i++) {
            key[i] = panel_key(keys[i]);
            pressed[i] = key[i] && !was[i];
            was[i] = key[i];
        }
        if (pressed[0])
            source(0);
        if (pressed[1])
            source(1);
        if (pressed[4])
            cd_command(eject, 0, 0);
        turn = panel_knob(KNOB_SELECT);
        if (media && files && (turn || pressed[2] || pressed[3])) {
            current = (int)umod((u32)(current + files * 8 + turn + pressed[2] - pressed[3]), (u32)files);
            play();
        } else if (!media && (turn || pressed[2] || pressed[3])) {
            frequency += 4 * turn + 20 * (pressed[2] - pressed[3]);
            if (frequency > 2158)
                frequency = 1758;
            if (frequency < 1758)
                frequency = 2158;
            r = radio_tune(frequency);
            rec(5, (u32)frequency, (u32)r);
            show(r ? "NOT TUNED" : "");
        }
        turn = panel_knob(KNOB_VOLUME);
        if (turn) {
            volume += turn;
            volume = volume < 0 ? 0 : volume > 30 ? 30 : volume;
            rec(6, (u32)volume, (u32)radio_volume(volume));
            show(0);
        }
        if (media) {
            r = sd_play_poll();
            if (r <= 0) {
                rec(4, (u32)current, (u32)r);
                if (r < 0) {
                    media = 0;
                    show("THE PLAYER STOPPED");
                } else {
                    current = (int)umod((u32)(current + 1), (u32)files);
                    play();
                }
            }
        }
        if (!key[5]) {
            held = 0;
        } else if (!held) {
            held = millis() | 1;
        } else if (millis() >= held && millis() - held > 2000 && firmware_installed()) {
            sd_play_stop();
            line(150, "TAKING THE PROGRAM OUT", 3, 2);
            console_puts("removing\n");
            if (firmware_remove()) {
                line(150, "THE FLASH DID NOT ERASE", 3, 2);
                held = 0;
            } else {
                console_puts("removed, restarting\n");
                delay_ms(500);
                restart();
            }
        } else if (millis() >= held && millis() - held > 2000) {
            /* From a card: RETURN is just the way back to the loader. */
            sd_play_stop();
            restart();
        }
    }
}
