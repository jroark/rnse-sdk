/* Sound as a stream of blocks (pcm_stream), the 192 and the 193: a
 * hands-off test for a unit on a bench, started from an SD card (the
 * 192's examples/sdboot, the 193's examples/sdload193).
 *
 * Two tones through the amplifier (radio_source(RADIO_DECODER)), 440 Hz
 * on the left and 880 Hz on the right, so that a swapped pair of
 * channels is heard as such:
 *
 *   part 1:  four seconds, the blocks filled as soon as there is room;
 *   part 2:  four seconds of 550 Hz left and 1100 Hz right, the program
 *            coming back only every 30 ms, as a program that has other
 *            work does (a block is 46 ms): no gap should be heard.
 *
 * The keys are answered all the while (panel_service): the display key
 * and CD EJECT work; RETURN held for two seconds stops the sound and
 * goes back to the SD loader where there is one.  What is measured goes
 * to the screen, the console and CAN 0x7F8 (test, item, a value of 32
 * bits and one of 16), as the other bench programs send it; on the 193
 * through the controller as the stock software left it:
 *
 *   0x60 0  what pcm_start said (0 = started), the unit (192/193)
 *   0x60 1  what radio_source said (0 = acknowledged), the answer's
 *           command (0x20 an acknowledgement)
 *   0x61 n  part n: samples a second (the output's real rate), gaps
 *   0x62 n  part n: on the 193 the DMA channel's status and the sound
 *           interface's status at the end of the part
 *   0xff    done; back to the SD loader after half a minute, by
 *           returning. */
#include "rnse.h"

#define RATE 44100
#define BLOCK 2048
#define BLOCKS 6

static s16 ring[2 * BLOCK * BLOCKS] __attribute__((aligned(32)));
static int row;

static char *put_dec(char *p, u32 v)
{
    char t[10];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *p++ = t[--n];
    return p;
}

static char *put_hex(char *p, u32 v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        *p++ = "0123456789ABCDEF"[(v >> i) & 15];
    return p;
}

static char *put_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

static void line(const char *text, u8 colour)
{
    int s = screen_width == 400 ? 1 : 2;
    int y = 2 + 10 * s * row;
    screen_fill(0, y - s, screen_width, 10 * s, 0);
    screen_text(2 * s, y, text, colour, s);
    console_puts(text);
    console_puts("\n");
    if (++row >= 22)
        row = 0;
}

static void rec(u8 test, u8 item, u32 value, u32 second)
{
    u8 d[8] = { test, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, (u8)(second >> 8), (u8)second };
    can_send_for(0x7f8, d, 8, 3000);
}

/* A sine from the phase's top eight bits: Bhaskara's approximation on
 * the half wave, 12000 at the top. */
static s16 sine(u32 phase)
{
    u32 x = (phase >> 24) & 127;
    s32 p = (s32)(x * (128 - x));
    s32 v = 16 * p * 12000 / (5 * 16384 - 4 * p);
    return (s16)((phase & 0x80000000) ? -v : v);
}

static u32 phase_l, phase_r;

static void fill(s16 *out, u32 left_hz, u32 right_hz)
{
    u32 i;
    /* The phase steps by hz * 2^32 / 44100 a sample. */
    for (i = 0; i < BLOCK; i++) {
        out[2 * i] = sine(phase_l);
        out[2 * i + 1] = sine(phase_r);
        phase_l += left_hz * 97392u;
        phase_r += right_hz * 97392u;
    }
}

/* One part: `ms` of the two tones, the program away for `away` ms
 * between its visits.  Returns the samples a second it measured. */
static u32 part(int n, u32 ms, u32 left_hz, u32 right_hz, u32 away)
{
    u32 start = millis(), first = 0, last = 0, blocks = 0, rate;
    int full = 0;
    char text[64], *p;
    while (millis() - start < ms) {
        s16 *b;
        while ((b = pcm_stream_block()) != 0) {
            fill(b, left_hz, right_hz);
            pcm_stream_ready();
            /* Once the ring has been full, a block that has room again
             * is a block that has been played: the rate is the blocks
             * between the first and the last such moment. */
            if (full) {
                if (!first)
                    first = micros() | 1;
                else
                    blocks++, last = micros();
            }
        }
        full = 1;
        panel_service();
        if (away) {
            u32 t = millis();
            while (millis() - t < away)
                ;
        }
    }
    /* (Blocks times 2048 times 1000 stays well inside 32 bits.) */
    rate = blocks && last - first >= 1000 ? blocks * BLOCK * 1000 / ((last - first) / 1000) : 0;
    rec(0x61, (u8)n, rate, pcm_gaps);
    p = put_str(text, n == 1 ? "PART 1: " : "PART 2 (AWAY 30 MS): ");
    p = put_dec(p, rate);
    p = put_str(p, " SAMPLES/S, GAPS ");
    p = put_dec(p, pcm_gaps);
    *p = 0;
    line(text, pcm_gaps ? 2 : 1);
    if (screen_width != 400) {
        u32 dma = REG32(0xff8011b4), ssi = REG32(0xff984004);
        rec(0x62, (u8)n, dma, (u16)(ssi >> 16));
        p = put_str(text, "  DMA ");
        p = put_hex(p, dma);
        p = put_str(p, " SSI ");
        p = put_hex(p, REG32(0xff984000));
        p = put_str(p, "/");
        p = put_hex(p, ssi);
        *p = 0;
        line(text, 1);
    }
    return rate;
}

int main(void)
{
    char text[64], *p;
    int r;
    u32 t;

    board_init();
    console_puts("\nrnse sdk: pcmstream\n");
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 200, 0);
    screen_fill(0, 0, screen_width, screen_height, 0);
    /* (On the 193 the reports go through the controller as the stock
     * software left it: can_init left a unit's bus silent there.) */
    if (screen_width == 400)
        can_init();
    panel_init();
    sd_idle = panel_service;
    display_idle = panel_service;
    /* RETURN held: the sound stopped before the loader takes over. */
    loader_leave = pcm_stop;
    line("RNSE SDK: PCMSTREAM.  RESULTS ON CAN 7F8", 2);
    line("LEFT 440 HZ, RIGHT 880; THEN 550 / 1100", 1);
    line("HOLD RETURN 2 S: BACK TO THE LOADER", 1);

    r = pcm_start(1);
    rec(0x60, 0, (u32)r, screen_width == 400 ? 192 : 193);
    p = put_str(text, "PCM_START: ");
    p = put_dec(p, (u32)(r < 0 ? -r : r));
    p = put_str(p, r ? " (FAILED)" : " (OK)");
    *p = 0;
    line(text, r ? 2 : 1);
    r = radio_source(RADIO_DECODER, 12);
    rec(0x60, 1, (u32)r, (u32)radio_answered);
    p = put_str(text, "AMPLIFIER: ");
    p = put_str(p, r ? "NO ANSWER" : "OK");
    p = put_str(p, ", ANSWER ");
    p = put_hex(p, (u32)radio_answered);
    *p = 0;
    line(text, r ? 2 : 1);

    if (pcm_stream(ring, BLOCK, BLOCKS) == 0) {
        part(1, 4000, 440, 880, 0);
        part(2, 4000, 550, 1100, 30);
    } else {
        line("PCM_STREAM FAILED", 2);
    }
    pcm_stop();
    rec(0xff, 0xff, millis(), 0);
    line("DONE.  WHICH TONES WERE HEARD, WHERE?", 2);
    line("BACK TO THE SD LOADER IN 30 S", 1);
    t = millis();
    while (millis() - t < 30000) {
        panel_service();
        if (panel_return_held())
            line("NO LOADER TO RETURN TO", 2);
        cpu_idle();
    }
    return 0;
}
