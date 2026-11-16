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
 * See docs/android-auto.md. */
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

static int put_word(int n, u16 w, u32 ms)
{
    u32 until = millis() + ms;
    while (REG8(STATUS) & full_bit[n])
        if ((s32)(millis() - until) > 0)
            return -1;
    REG16(port[n]) = w;
    return 0;
}

int pcm_start(int n)
{
    u32 i, count = sizeof program / sizeof program[0];
    if (screen_width != 400 || n < 0 || n > 1)
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
    /* The controller reads memory, not the cache. */
    end = (u32)lr + 4 * frames;
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
static s16 *ring;
static u32 ring_frames, ring_blocks, ring_fill;
static volatile int ring_play = -1;
static volatile u32 ring_want;
static volatile u8 ring_ready[PCM_STREAM_MAX];

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
        ring_ready[p] = 0;
        ring_want = ((u32)p + 1) % ring_blocks;
    }
    if (ring_ready[ring_want]) {
        ring_play = (int)ring_want;
        arm(ring + 2 * ring_frames * ring_want, ring_frames);
    } else {
        ring_play = -1;
        arm(silence, SILENCE);
    }
}

int pcm_stream(s16 *buf, u32 frames, u32 blocks)
{
    u32 a, i;
    if (which < 0 || blocks < 2 || blocks > PCM_STREAM_MAX || ((u32)buf & 31) || (frames & 7))
        return -1;
    pcm_stream_stop();
    ring = buf;
    ring_frames = frames;
    ring_blocks = blocks;
    ring_fill = 0;
    ring_want = 0;
    ring_play = -1;
    for (i = 0; i < blocks; i++)
        ring_ready[i] = 0;
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
}

void pcm_stream_stop(void)
{
    if (!ring)
        return;
    ring = 0;
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
