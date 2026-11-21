/* Sound as samples (PCM) out of a card processor's converter: the 192.
 *
 * The 192's main processor has no sound output of its own; what the
 * unit plays of its own comes out of the converter of one of the two
 * card processors (NEC uPD77115A) and goes to the radio processor's
 * amplifier (radio_source(RADIO_DECODER, ...)).  pcm_start loads a
 * program of our own into a processor in place of the stock software's
 * MP3 player: it takes 16-bit words from the host port and hands them
 * to its audio output, left then right, one stereo sample each period
 * of the output's clock, 44.1 kHz from the card board's 22.5792 MHz
 * crystal (512 periods a sample).  Both of its waits are the
 * processor's own: a read of the host data register waits for the
 * host's word (HST.HAWE), a write to the audio data register waits for
 * room (ASST bit 8), so the converter's clock paces everything and the
 * host only has to keep words coming.
 *
 * The words come either by the CPU (pcm_put), which waits with them,
 * or by the DMA controller (pcm_dma), which the processor's "can take a
 * word" pin paces: the stock software's driver moves its words that
 * way, and the registers are set as it sets them (channel 7 for
 * processor 0, 3 for processor 1; the request selector latches at
 * 0x12d00007 and 0x12d00003; DMAOR's on-demand mode).
 *
 * The processor's card interface is not used by this program: a card
 * in that slot cannot be read while it plays (pcm_stop, then sd_init).
 * See sdbridge/docs/android-auto.md.
 *
 * The 193 (further down, "The 193") has a sound output of its own, a
 * serial sound interface fed by a DMA channel, and plays the same
 * streams (pcm_stream) through it. */
#include "rnse.h"

#define STATUS 0xb2400001 /* bits 2, 0: nothing to read; 3, 1: cannot take a word */
#define LATCH 0xb6000004  /* bits 2, 3: the processors' reset lines, low = reset */
#define DMAOR 0xffa00040

static const u32 port[2] = { 0xb5000002, 0xb500004a };
static const u32 dma[2] = { 0xffa00080, 0xffa00030 };       /* SAR of channel 7, 3 */
static const u32 selector[2] = { 0xb2d00007, 0xb2d00003 };
static const u8 select_on[2] = { 0x23, 0x24 };
static const u8 full_bit[2] = { 8, 2 };

/* Instruction words from 0x200; see src/dev/upd77115.rs for the
 * encodings (load immediate 0x38, load and store 0x48, relative jump
 * 0x2c). */
static const u32 program[] = {
    /* 0x200: the reset vector, then fifteen more of four words each,
     * every one a return from interrupt (none is enabled). */
    0x2c002000, 0, 0, 0,          /* jmp 0x240 */
    0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0,
    0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0,
    0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0,
    0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0,
    0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0,
    /* 0x240 */
    0x38010c00,                   /* r0l = 0x0c00 */
    0x48103807,                   /* HST = r0l: 16-bit words, reads wait for one */
    0x3801d762,                   /* r0l = 0xd762 */
    0x48103802,                   /* ASST = r0l: output on, master, 16 bits a
                                     channel, 512 clocks a sample, writes wait */
    0x38010042,                   /* r0l = 0x42 */
    0x48103801,                   /* SST = r0l: the output's pins driven */
    /* 0x246 */
    0x48113806,                   /* r0l = HDT: left */
    0x48103800,                   /* ASDT = r0l */
    0x48113806,                   /* r0l = HDT: right */
    0x48103800,                   /* ASDT = r0l */
    0x2c7ffe00,                   /* jmp 0x246 */
};

static int which = -1;

/* The 193: the system-on-chip's serial sound interface (SSI) at
 * 0xff984000, fed 32-bit words (the left sample in the high half, the
 * right one in the low half: in this big-endian memory, a stereo
 * sample of the kit's left-then-right s16 pairs as it is) by DMA
 * channel 3, which has two sets of source, destination and count and
 * goes on from one to the other by itself (rnse docs/hardware-193.md,
 * "DMA channels", "Sound output").  The registers are written as the
 * stock software's sound driver writes them to start an MP3 file at
 * 44.1 kHz (seen in the emulator, V575: the audio clock, the
 * interface's set-up, the channel's mode, the sets with physical
 * source addresses, the channel run, then the interface on).  Unlike
 * that driver this code takes no interrupt: the channel's end-of-set
 * interrupt (INTEVT 0x980) is switched off at its source, and the next
 * block is loaded into the free set whenever the program asks for a
 * block to fill (pcm_stream_block) or hands one over
 * (pcm_stream_ready), which it must do at least once a block.  When
 * the channel has run dry it is started again from the next ready
 * block (pcm_gaps counts those times). */
#define SSI 0xff984000
#define SSI_SETUP 0x000bc532  /* 44.1 kHz, as the stock driver; off */
#define SSI_ON 0x10000001     /* DMA, enabled (the driver also enables its
                                 underflow and overflow interrupts: not here) */
#define AUDIO_CLOCK 0xff972000
#define AUDIO_CLOCK_44K 0x3dff
#define ADMA 0xff801180       /* channel 3 */
#define ADMA_COMMON 0xff801400
#define ADMA_BIT 0x40         /* channel 3's bit in the common registers */
#define ADMA_MODE 0x05050022

u32 pcm_gaps;
static int on_193;
static int set_block[2] = { -1, -1 }; /* ring block in the first, second set */

static void adma_load(int second, const s16 *lr, u32 frames)
{
    u32 set = ADMA + (second ? 0x0c : 0);
    REG32(set + 0) = (u32)lr & 0x1fffffff;
    REG32(set + 4) = SSI + 8;
    REG32(set + 8) = frames;
}

static void adma_halt(void)
{
    REG32(ADMA + 0x30) = 1;
    REG32(ADMA + 0x2c) = 0;
    REG32(SSI) = SSI_SETUP;
    REG32(SSI + 4) = 0;
    REG32(ADMA_COMMON + 0x14) = ADMA_BIT;
}

static u32 ring_next_play; /* the next block to be played, in ring order */
static s16 *ring;
static u32 ring_frames, ring_blocks, ring_fill;
static volatile u8 ring_ready[PCM_STREAM_MAX];

/* Frees the blocks the channel is done with and loads the next ones. */
static void service_193(void)
{
    u32 status;
    int second, b;
    if (!ring)
        return;
    status = REG32(ADMA + 0x34);
    if (REG32(ADMA_COMMON + 0x10) & ADMA_BIT)
        REG32(ADMA_COMMON + 0x14) = ADMA_BIT;
    if (!(status & 1)) {
        /* Not running: both sets are done. */
        for (second = 0; second < 2; second++)
            if (set_block[second] >= 0) {
                ring_ready[set_block[second]] = 0;
                set_block[second] = -1;
            }
        if (!ring_ready[ring_next_play])
            return;
        if (REG32(SSI) & 1)
            pcm_gaps++;
        /* As the stock driver starts: the interface off, the channel
         * set up and running, the interface on. */
        REG32(SSI) = SSI_SETUP;
        REG32(SSI + 4) = 0;
        REG32(ADMA + 0x28) = ADMA_MODE;
        b = (int)ring_next_play;
        adma_load(0, ring + 2 * ring_frames * (u32)b, ring_frames);
        set_block[0] = b;
        ring_next_play = (ring_next_play + 1) % ring_blocks;
        REG32(ADMA + 0x2c) = 1;
        REG32(SSI) = SSI_SETUP | SSI_ON;
        status = REG32(ADMA + 0x34);
    }
    if (!(status & 1) || (status & 2))
        return;
    /* Running with nothing loaded behind the set in use: the other set
     * is free, and the block that was in it has been played. */
    second = (status & 0x40) ? 1 : 0;
    if (set_block[second] >= 0) {
        ring_ready[set_block[second]] = 0;
        set_block[second] = -1;
    }
    if (!ring_ready[ring_next_play])
        return;
    b = (int)ring_next_play;
    adma_load(second, ring + 2 * ring_frames * (u32)b, ring_frames);
    set_block[second] = b;
    ring_next_play = (ring_next_play + 1) % ring_blocks;
    REG32(ADMA + 0x2c) = 3;
}

static int put_word(int n, u16 w, u32 ms)
{
    u32 until = millis() + ms;
    while (REG8(STATUS) & full_bit[n])
        if ((s32)(millis() - until) > 0)
            return -1;
    REG16(port[n]) = w;
    return 0;
}

/* The 193's sound path around the interface, as the stock software's
 * sound driver opens and closes it (V575 1050: its open at 0x8062bae0,
 * its close at 0x8062bb6a, its one-time set-up at 0x8062b9bc; read off
 * the code and the emulator's log of it, 2026-11-21):
 *
 *   - 0xff9900b8 bit 4 on (after the key at 0xff900864): the interface's
 *     module.  Without it the interface takes no write (the bench 193:
 *     its control register read 0 after 0x100bc533 was written);
 *   - port 0xff909000 bit 29 low, a moment, high: a line out to the
 *     sound path, low while closed;
 *   - 0xff90301c = 0x1b;
 *   - GPIO 0xff93e008 bit 13 low while playing, high when closed (a
 *     mute, by all appearances; the driver's set-up makes it an output,
 *     0xff93e004 bit 13).
 *
 * The one-time set-up (the ports, the mute an output and on, and the
 * audio block at 0xff985000/0xff986000 that the software's start makes
 * with 0xff9900b8 bits 5 and 6) is the stock software's: a program it
 * started finds it made.  Only when those bits are off (a program that
 * nothing set up, such as one the boot ROM starts) is the block's part
 * made here, as that start makes it. */
#define AUDIO_KEY 0xff900864
#define AUDIO_MODULES 0xff9900b8
#define AUDIO_PORT 0xff909000      /* bit 29 */
#define AUDIO_MUTE_DIR 0xff93e004  /* bit 13 */
#define AUDIO_MUTE 0xff93e008      /* bit 13 */

static void audio_key(void)
{
    if (!(REG32(AUDIO_KEY) & 1))
        REG32(AUDIO_KEY) = 0xf0a50f5a;
}

static void audio_open_193(void)
{
    if ((REG32(AUDIO_MODULES) & 0x60) != 0x60) {
        REG32(AUDIO_CLOCK) = 0x3c00;
        REG16(0xff973000) = 0x0012;
        audio_key();
        REG32(AUDIO_MODULES) |= 0x40;
        REG32(0xff986000) = 0x0009d533;
        REG32(AUDIO_MODULES) |= 0x20;
        REG32(0xff985000) = 0x00091100;
        REG32(0xff986010) = 1;
        REG32(0xff986014) = 0x20;
        REG32(0xff986018) = 0x20;
        REG32(0xff986008) = 0;
        REG32(0xff986004) = 0;
        REG32(AUDIO_MUTE_DIR) |= 0x2000;
        REG32(AUDIO_MUTE) |= 0x2000;
    }
    audio_key();
    /* Bit 2 of the port's data (0xff909004), which the driver's one-time
     * set-up clears: high on the bench 193 when the stock software had
     * played a card's MP3 before handing over, and with it the sound
     * came out; low and silent otherwise (2026-11-21).  Being tried
     * again: its first trial was confounded (the bridge was out, and
     * without it the viewer's front went dark whatever it did). */
    REG32(AUDIO_PORT + 4) |= 0x4;
    REG32(AUDIO_PORT) &= ~0x20000000u;
    delay_ms(1);
    REG32(AUDIO_PORT) |= 0x20000000u;
    delay_ms(1);
    REG32(AUDIO_MODULES) |= 0x10;
    REG32(0xff90301c) = 0x1b;
    REG32(SSI) = SSI_SETUP;
    REG32(AUDIO_MUTE) &= ~0x2000u;
}

static void audio_close_193(void)
{
    REG32(SSI) &= 0xe3ff3ffe;
    REG32(ADMA + 0x30) = 1;
    REG32(AUDIO_MUTE) |= 0x2000;
    REG32(AUDIO_PORT) &= ~0x20000000u;
}

int pcm_start(int n)
{
    u32 i, count = sizeof program / sizeof program[0];
    if (screen_width != 400) {
        /* The 193: one output, whatever n says. */
        pcm_stop();
        REG32(ADMA_COMMON + 0x18) &= ~(u32)(ADMA_BIT | ADMA_BIT << 1);
        adma_halt();
        audio_open_193();
        REG32(AUDIO_CLOCK) = AUDIO_CLOCK_44K;
        REG32(ADMA + 0x28) = ADMA_MODE;
        on_193 = 1;
        return 0;
    }
    if (n < 0 || n > 1)
        return -1;
    pcm_stop();
    REG32(LATCH) &= ~(4u << n);
    delay_ms(20);
    REG32(LATCH) |= 4u << n;
    delay_ms(20);
    /* The boot ROM's download, as the stock driver sends it: a word
     * that does not count, the number of instructions, the host port's
     * mode, then the instructions, low half first. */
    if (put_word(n, 1, 200) || put_word(n, (u16)count, 200) || put_word(n, 0x0c00, 200))
        return -2;
    for (i = 0; i < count; i++)
        if (put_word(n, (u16)program[i], 200) || put_word(n, (u16)(program[i] >> 16), 200))
            return -3;
    which = n;
    return 0;
}

void (*pcm_idle)(void);

int pcm_put(const s16 *lr, u32 frames)
{
    u32 i;
    if (which < 0)
        return -1;
    for (i = 0; i < 2 * frames; i++) {
        if (put_word(which, (u16)lr[i], 100))
            return -2;
        /* (The keys, now and then: the processor holds two samples, so
         * whatever pcm_idle does is heard as a gap if it takes long.) */
        if (pcm_idle && !(i & 4095))
            pcm_idle();
    }
    return 0;
}

int pcm_dma(const s16 *lr, u32 frames)
{
    u32 a, end, base;
    if (which < 0)
        return -1;
    if (pcm_dma_busy())
        return -2;
    base = dma[which];
    /* The controller reads memory, not the cache (nothing to do for
     * samples written through P2, which the cache does not hold). */
    end = (u32)lr + 4 * frames;
    if (((u32)lr >> 29) != 5)
        for (a = (u32)lr & ~31u; a < end; a += 32)
            __asm__ volatile("ocbwb @%0" : : "r"(a) : "memory");
    /* On-demand mode (DDT, all eight channels), round-robin, on. */
    REG32(DMAOR) |= 0xc301;
    REG32(base + 0) = (u32)lr;
    REG32(base + 4) = port[which];
    REG32(base + 8) = 2 * frames;
    REG8(selector[which]) = select_on[which];
    /* 16-bit words, the source counting up, the destination fixed, on
     * the processor's request; no interrupt at the end. */
    REG32(base + 12) = 0x000a1021;
    return 0;
}

u32 pcm_dma_left(void)
{
    if (which < 0 || !(REG32(dma[which] + 12) & 1))
        return 0;
    return REG32(dma[which] + 8);
}

int pcm_dma_busy(void)
{
    u32 base, chcr;
    if (which < 0)
        return 0;
    base = dma[which];
    chcr = REG32(base + 12);
    if (!(chcr & 1))
        return 0;
    if (!(chcr & 2))
        return 1;
    /* Done: put things back as the stock driver does. */
    REG32(base + 12) = 0x00080020;
    REG32(DMAOR) &= ~0x0300u;
    REG8(selector[which]) |= 0x1f;
    REG8(selector[which]) |= 0x40;
    return 0;
}

/* Streaming: a ring of blocks that the DMA controller plays one after
 * the other without a gap, each transfer started from the end of the
 * last one's interrupt (DMTE7 for processor 0, DMTE3 for 1, at level
 * 9 through IPRC; hw192.c hands them to pcm_interrupt).  The program
 * fills the blocks in order (pcm_stream_block, pcm_stream_ready); when
 * the next one is not ready in time a block of silence plays instead,
 * and the ring goes on with it afterwards. */
#define IPRC 0xffd0000c
#define SILENCE 256
static s16 silence[2 * SILENCE] __attribute__((aligned(32)));
static volatile int ring_play = -1;
static volatile u32 ring_want;
static int ring_loop;

static void arm(const s16 *lr, u32 frames)
{
    u32 base = dma[which];
    /* Stopped, its end flag cleared: the flag clears by a write of 0
     * after it has been read as 1 (the SH-4 manual), so read first. */
    (void)REG32(base + 12);
    REG32(base + 12) = 0x000a1024;
    REG32(base + 0) = (u32)lr;
    REG32(base + 4) = port[which];
    REG32(base + 8) = 2 * frames;
    REG32(base + 12) = 0x000a1025; /* as pcm_dma, and an interrupt at the end */
}

void pcm_interrupt(void)
{
    int p = ring_play;
    if (which < 0 || !ring)
        return;
    if (p >= 0) {
        if (!ring_loop)
            ring_ready[p] = 0;
        ring_want = ((u32)p + 1) % ring_blocks;
    }
    if (ring_ready[ring_want]) {
        ring_play = (int)ring_want;
        arm(ring + 2 * ring_frames * ring_want, ring_frames);
    } else {
        if (p >= 0)
            pcm_gaps++;
        ring_play = -1;
        arm(silence, SILENCE);
    }
}

int pcm_stream(s16 *buf, u32 frames, u32 blocks)
{
    u32 a, i;
    if ((which < 0 && !on_193) || blocks < 2 || blocks > PCM_STREAM_MAX || ((u32)buf & 31) || (frames & 7))
        return -1;
    pcm_stream_stop();
    ring_frames = frames;
    ring_blocks = blocks;
    ring_fill = 0;
    ring_want = 0;
    ring_play = -1;
    ring_next_play = 0;
    pcm_gaps = 0;
    for (i = 0; i < blocks; i++)
        ring_ready[i] = 0;
    ring = buf;
    if (on_193)
        return 0;
    for (i = 0; i < 2 * SILENCE; i++)
        silence[i] = 0;
    for (a = (u32)silence; a < (u32)silence + sizeof silence; a += 32)
        __asm__ volatile("ocbwb @%0" : : "r"(a) : "memory");
    REG16(IPRC) = (u16)((REG16(IPRC) & ~0x0f00) | 0x0900);
    REG32(DMAOR) |= 0xc301;
    REG8(selector[which]) = select_on[which];
    arm(silence, SILENCE);
    return 0;
}

s16 *pcm_stream_block(void)
{
    if (on_193)
        service_193();
    if (!ring || ring_ready[ring_fill])
        return 0;
    return ring + 2 * ring_frames * ring_fill;
}

void pcm_stream_ready(void)
{
    u32 a, start, end;
    if (!ring || ring_ready[ring_fill])
        return;
    /* The controller reads memory, not the cache. */
    start = (u32)(ring + 2 * ring_frames * ring_fill);
    end = start + 4 * ring_frames;
    for (a = start; a < end; a += 32)
        __asm__ volatile("ocbwb @%0" : : "r"(a) : "memory");
    ring_ready[ring_fill] = 1;
    ring_fill = (ring_fill + 1) % ring_blocks;
    if (on_193)
        service_193();
}

int pcm_loop(s16 *buf, u32 frames, u32 blocks)
{
    u32 a, i;
    int r;
    if (on_193)
        return -1; /* (not yet: pcm_stream only) */
    /* Nothing of the ring left in the cache: a dirty line written back
     * later would put old samples over the program's. */
    for (a = (u32)buf & ~31u; a < (u32)buf + 4 * frames * blocks; a += 32)
        __asm__ volatile("ocbp @%0" : : "r"(a) : "memory");
    r = pcm_stream(buf, frames, blocks);
    if (r)
        return r;
    for (i = 0; i < blocks; i++)
        ring_ready[i] = 1;
    ring_loop = 1;
    return 0;
}

u32 pcm_loop_pos(void)
{
    int p;
    u32 left;
    if (!ring || !ring_loop)
        return 0;
    /* The block and the words still to go of it, read again when the
     * interrupt moved on to the next block in between. */
    do {
        p = ring_play;
        left = REG32(dma[which] + 8);
    } while (p != ring_play);
    if (p < 0)
        return 0;
    if (left > 2 * ring_frames)
        left = 2 * ring_frames;
    return umod((u32)p * ring_frames + ring_frames - left / 2, ring_frames * ring_blocks);
}

void pcm_stream_stop(void)
{
    if (!ring)
        return;
    ring = 0;
    ring_loop = 0;
    if (on_193) {
        adma_halt();
        set_block[0] = set_block[1] = -1;
        return;
    }
    REG16(IPRC) &= (u16)~0x0f00;
    if (which >= 0) {
        REG32(dma[which] + 12) = 0x00080020;
        REG32(DMAOR) &= ~0x0300u;
        REG8(selector[which]) |= 0x1f;
        REG8(selector[which]) |= 0x40;
    }
}

void pcm_stop(void)
{
    int i;
    if (on_193) {
        pcm_stream_stop();
        adma_halt();
        audio_close_193();
        on_193 = 0;
        return;
    }
    if (which < 0)
        return;
    pcm_stream_stop();
    if (REG32(dma[which] + 12) & 1) {
        REG32(dma[which] + 12) = 0x00080020;
        REG32(DMAOR) &= ~0x0300u;
        REG8(selector[which]) |= 0x1f;
        REG8(selector[which]) |= 0x40;
    }
    /* Silence before the reset, so that the output does not stop on
     * whatever sample was last (two frames: through the processor's
     * register and its output's). */
    for (i = 0; i < 8; i++)
        if (put_word(which, 0, 20))
            break;
    REG32(LATCH) &= ~(4u << which);
    which = -1;
}
