/* Sound as samples from a program of our own (src/pcm.c), the 192: a
 * hands-off test for a unit on a bench, from an SD card (the SD loader,
 * examples/sdboot).
 *
 * For each card processor in turn, two tones of two seconds, on both
 * channels, through the amplifier (radio_source(RADIO_DECODER)), fed
 * by DMA, which leaves the CPU to the keys meanwhile:
 *
 *   processor 0:  440 Hz by one DMA transfer (pcm_dma), 550 Hz by DMA
 *                 transfers of 4096 samples one after the other, as a
 *                 stream would come;
 *   processor 1:  880 and 1100 Hz the same ways.
 *
 * Which of them are heard says which processor's converter reaches the
 * amplifier.  The keys are answered all the while (panel_service): the
 * display key and CD EJECT work, and RETURN held for two seconds stops
 * the sound and goes back to the SD loader.  What is measured goes to
 * the screen and to CAN 0x7F8 (test, item, a value of 32 bits and one
 * of 16), as the other bench programs send it:
 *
 *   0x40 + 3 * processor + way (way 1: one transfer, 2: blocks):
 *     0  what pcm_start said (0 = the program was taken), the frequency
 *     1  microseconds the two seconds of samples took to be taken
 *     2  samples a second that makes (the converter's real rate)
 *     3  per mille of the CPU left over meanwhile
 *     4  for the blocks: the longest gap between two transfers, in
 *        microseconds, and the number of transfers
 *   0x50  0: microseconds to resample one second of 48 kHz stereo to
 *         44.1 kHz (linear); 1: the same to mix two such streams
 *   0xff  done; back to the SD loader after half a minute, by
 *         returning. */
#include "rnse.h"

#define RATE 44100
#define SECONDS 2
#define FRAMES (RATE * SECONDS)
#define BLOCK 4096

/* Samples: two seconds of stereo, in RAM the program does not occupy
 * (below the stock loader's place, above the way back's scratch). */
#define BUFFER ((s16 *)0x8c800000)
/* For the resampler's test: 48000 frames in, 44100 out. */
#define IN48 ((s16 *)0x8cc00000)
#define OUT44 ((s16 *)0x8cd00000)

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

static char *put_str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

static void line(const char *text, u8 colour)
{
    int y = 2 + 10 * row;
    screen_fill(0, y - 1, screen_width, 10, 0);
    screen_text(2, y, text, colour, 1);
    console_puts(text);
    console_puts("\n");
    if (++row >= 23)
        row = 0;
}

static void rec(u8 test, u8 item, u32 value, u32 second)
{
    u8 d[8] = { test, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, (u8)(second >> 8), (u8)second };
    can_send_for(0x7f8, d, 8, 3000);
}

/* A sine from the phase's top eight bits: Bhaskara's approximation on
 * the half wave, 12000 at the top (about -8.7 dB of full scale). */
static s16 sine(u32 phase)
{
    u32 x = (phase >> 24) & 127;          /* 0..127 is 0..pi */
    s32 p = (s32)(x * (128 - x));         /* up to 4096 */
    s32 v = 16 * p * 12000 / (5 * 16384 - 4 * p);
    return (s16)((phase & 0x80000000) ? -v : v);
}

static void tone(s16 *out, u32 frames, u32 hz)
{
    /* The phase steps by hz * 2^32 / 44100 a sample. */
    u32 step = hz * 97392u, phase = 0, i;
    for (i = 0; i < frames; i++, phase += step) {
        s16 v = sine(phase);
        out[2 * i] = v;
        out[2 * i + 1] = v;
    }
}

/* How many rounds of a little loop the CPU gets through in a
 * millisecond (times 1000) with nothing else going on: the yardstick
 * for what is left over during a transfer. */
static u32 per_ms;

static u32 spin(u32 rounds)
{
    volatile u32 sink = 0;
    u32 i;
    for (i = 0; i < rounds; i++)
        sink += i;
    return sink;
}

static void calibrate(void)
{
    /* The same rounds as the waits below (a little work and a look at
     * a register of the DMA controller), timed at the ends only. */
    u32 t = micros(), n;
    for (n = 0; n < 20000; n++) {
        spin(64);
        (void)pcm_dma_busy();
    }
    t = micros() - t;
    /* Rounds a millisecond, times 1000. */
    per_ms = t ? n * 1000 / (t / 1000) : 0;
}

static void say(const char *what, u32 a, const char *unit)
{
    char text[64], *q = put_str(text, what);
    q = put_dec(q, a);
    q = put_str(q, unit);
    *q = 0;
    line(text, 1);
}

static void way(int dsp, int w, u32 hz)
{
    static const char *const names[3] = { "BY THE CPU", "BY ONE DMA TRANSFER", "BY DMA IN BLOCKS" };
    u8 test = (u8)(0x40 + 3 * dsp + w);
    u32 t, spent, rounds = 0, gaps = 0, longest = 0, transfers = 0;
    int r = 0;
    char text[64], *q;

    tone(BUFFER, FRAMES, hz);
    q = put_str(text, dsp ? "PROCESSOR 1, " : "PROCESSOR 0, ");
    q = put_str(q, names[w]);
    q = put_str(q, ": ");
    q = put_dec(q, hz);
    q = put_str(q, " HZ");
    *q = 0;
    line(text, 2);

    t = micros();
    if (w == 0) {
        r = pcm_put(BUFFER, FRAMES);
    } else if (w == 1) {
        r = pcm_dma(BUFFER, FRAMES);
        while (!r && pcm_dma_busy()) {
            spin(64);
            if (!(++rounds & 255))
                panel_service();
        }
    } else {
        u32 done = 0, end = 0;
        while (!r && done < FRAMES) {
            u32 n = FRAMES - done < BLOCK ? FRAMES - done : BLOCK;
            if (transfers) {
                u32 gap = micros() - end;
                gaps += gap;
                if (gap > longest)
                    longest = gap;
            }
            r = pcm_dma(BUFFER + 2 * done, n);
            transfers++;
            done += n;
            while (!r && pcm_dma_busy()) {
                spin(64);
                /* The keys, but not when the block is about to end: the
                 * next one has to start within a sample. */
                if (!(++rounds & 255) && pcm_dma_left() > 1024)
                    panel_service();
            }
            end = micros();
        }
    }
    spent = micros() - t;
    rec(test, 0, (u32)r, hz);
    rec(test, 1, spent, 0);
    rec(test, 2, spent ? FRAMES * 10000u / (spent / 100) : 0, 0);
    if (r) {
        say("  FAILED: ", (u32)-r, "");
        return;
    }
    say("  TOOK US ", spent, "");
    say("  SAMPLES A SECOND ", spent ? FRAMES * 10000u / (spent / 100) : 0, "");
    if (w) {
        /* What the CPU got through, against what it gets through in the
         * same time with nothing else going on. */
        u32 left = per_ms ? rounds * 1000 / (spent / 1000) * 1000 / per_ms : 0;
        rec(test, 3, left, 0);
        say("  CPU LEFT OVER, PER MILLE ", left, "");
    }
    if (w == 2) {
        rec(test, 4, longest, transfers);
        say("  LONGEST GAP US ", longest, "");
    }
    (void)gaps;
}

/* 48 kHz stereo to 44.1 kHz, linear: the step from one output sample
 * to the next is 48000/44100 input samples, in 1/65536. */
static void resample(const s16 *in, u32 in_frames, s16 *out, u32 out_frames)
{
    u32 pos = 0, step = 71331, i;   /* 65536 * 48000 / 44100 */
    for (i = 0; i < out_frames; i++, pos += step) {
        u32 k = pos >> 16, f = pos & 0xffff;
        const s16 *a = in + 2 * k;
        if (k + 1 >= in_frames) {
            out[2 * i] = a[0];
            out[2 * i + 1] = a[1];
            continue;
        }
        out[2 * i] = (s16)(a[0] + (((s32)(a[2] - a[0]) * (s32)f) >> 16));
        out[2 * i + 1] = (s16)(a[1] + (((s32)(a[3] - a[1]) * (s32)f) >> 16));
    }
}

static void mix(s16 *a, const s16 *b, u32 samples)
{
    u32 i;
    for (i = 0; i < samples; i++) {
        s32 v = a[i] + b[i];
        a[i] = (s16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

int main(void)
{
    static const u32 hz[2][3] = { { 0, 440, 550 }, { 0, 880, 1100 } };
    int dsp, w, r;
    u32 t;

    board_init();
    console_puts("\nrnse sdk: pcmtest\n");
    if (screen_width != 400) {
        console_puts("the 192 only\n");
        return 1;
    }
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 200, 0);
    screen_fill(0, 0, screen_width, screen_height, 0);
    can_init();
    panel_init();
    sd_idle = panel_service;
    display_idle = panel_service;
    pcm_idle = panel_service;
    /* RETURN held: the sound stopped before the loader takes over. */
    loader_leave = pcm_stop;
    line("RNSE SDK: PCMTEST.  RESULTS ON CAN 7F8", 2);
    line("HOLD RETURN 2 S: BACK TO THE LOADER", 1);
    calibrate();

    /* What a stream from a phone would cost besides: its 48 kHz to the
     * converter's 44.1, and a second stream (guidance) mixed in. */
    tone(IN48, 48000, 1000);
    t = micros();
    resample(IN48, 48000, OUT44, 44100);
    t = micros() - t;
    rec(0x50, 0, t, 0);
    say("RESAMPLING 1 S, US ", t, "");
    t = micros();
    mix(OUT44, IN48, 2 * 44100);
    t = micros() - t;
    rec(0x50, 1, t, 0);
    say("MIXING 1 S, US ", t, "");

    r = radio_source(RADIO_DECODER, 12);
    say("AMPLIFIER: ", (u32)r, r ? " (NO ANSWER)" : " (OK)");
    for (dsp = 0; dsp < 2; dsp++) {
        r = pcm_start(dsp);
        if (r) {
            rec((u8)(0x40 + 3 * dsp), 0, (u32)r, 0);
            say(dsp ? "PROCESSOR 1 DID NOT START: " : "PROCESSOR 0 DID NOT START: ", (u32)-r, "");
            continue;
        }
        /* (Way 0, the CPU handing the samples over itself, is left out
         * here: it cannot answer the keys for the two seconds.) */
        for (w = 1; w < 3; w++)
            way(dsp, w, hz[dsp][w]);
        pcm_stop();
    }
    rec(0xff, 0xff, millis(), 0);
    line("DONE.  WHICH TONES WERE HEARD?", 2);
    line("BACK TO THE SD LOADER IN 30 S", 1);
    t = millis();
    while (millis() - t < 30000) {
        panel_service();
        if (panel_return_held())
            line("NO LOADER TO RETURN TO", 2);
        cpu_idle();
    }
    /* To the loader that started the program (examples/sdboot), which
     * waits for the next card. */
    return 0;
}
