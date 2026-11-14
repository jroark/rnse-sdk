/* The 192's two card processors (NEC uPD77115A, one behind each SD
 * slot), asked about themselves.
 *
 * They do nothing until a program is loaded into them through their
 * host port.  This loads one of a few instructions (below, written out
 * by hand; nothing of the stock software's) that
 *
 *   - sends the levels on the processor's eight port pins, which are
 *     all inputs after a reset: pins 7 to 4 are the multiplier of its
 *     clock, pins 1 and 0 the boot mode, as the board straps them;
 *   - sends its clock control and host status registers;
 *   - runs a loop of a known number of clocks on the clock input (the
 *     22.5792 MHz crystal of the card board) and says when it is done;
 *   - switches to the multiplied clock, the way the stock program does,
 *     and runs the same loop again.
 *
 * The main processor times the two loops with its timer 0.  The first
 * gives the main processor's own peripheral clock against a crystal
 * whose frequency is printed on it; the two together give the card
 * processor's multiplier a second time.
 *
 * Nothing is written to a card, and the processors' pins stay inputs.
 * Both are put back into reset afterwards, as board_init() left them. */
#include "rnse.h"
#include "dsp.h"

#define STATUS 0xb2400001 /* bits 2, 0: nothing to read; 3, 1: cannot write */
#define LATCH 0xb6000004  /* bits 2, 3: the processors' reset lines, low = reset */
#define TCNT0 0xffd8000c
#define TCOR0 0xffd80008
static const u32 port[2] = { 0xb5000002, 0xb500004a };

/* How often the loop of 32768 clocks is run. */
#define LOOPS 1024

static const u32 program[] = {
    /* 0x200: the reset vector, then fifteen more of four words each,
     * every one a return from interrupt. */
    0x2c002000, 0, 0, 0,          /* jmp 0x240 */
    0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0,
    0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0,
    0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0,
    0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0,
    0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0, 0x26000000, 0, 0, 0,
    /* 0x240 */
    0x48113804,                   /* r0l = port data */
    0x48103806,                   /* host data = r0l */
    0x4811382e,                   /* r0l = clock control */
    0x48103806,
    0x48113807,                   /* r0l = host status */
    0x48103806,
    0x48113806,                   /* r0l = host data: how many loops */
    0x10050000,                   /* loop r0l { the next five } */
    0x1c007ffc,                   /*   rep 0x7ffc */
    0, 0, 0, 0,                   /*     nop; and three more: 32768 clocks */
    0x48103806,                   /* host data = r0l: done */
    0x38810100,                   /* r1l = 0x100 */
    0x4890382e,                   /* clock control = r1l: multiplier on */
    0x1c004e20, 0,                /* rep 20000: nop, while it settles */
    0x38810180,                   /* r1l = 0x180 */
    0x4890382e,                   /* clock control = r1l: and selected */
    0, 0,
    0x48903806,                   /* host data = r1l: switched */
    0x48113806,                   /* r0l = host data: how many loops */
    0x10050000,
    0x1c007ffc,
    0, 0, 0, 0,
    0x48103806,                   /* host data = r0l: done */
    0x48113806,                   /* and wait for a word that never comes */
    0x2c7fff80,                   /* jmp back one */
};

u32 dsp_values[2][DSP_VALUES];

/* Counts of timer 0 since the tick started: it counts down from its
 * reload value every 10 ms. */
static u32 counts(void)
{
    u32 ms, c;
    do {
        ms = millis();
        c = REG32(TCNT0);
    } while (ms != millis());
    return ms / 10 * (REG32(TCOR0) + 1) + (REG32(TCOR0) - c);
}

static int wait_low(u8 mask, u32 ms)
{
    u32 until = millis() + ms;
    while (REG8(STATUS) & mask)
        if ((s32)(millis() - until) > 0)
            return -1;
    return 0;
}

static int put(int n, u16 w)
{
    if (wait_low(n ? 2 : 8, 200))
        return -1;
    REG16(port[n]) = w;
    return 0;
}

static int get(int n, u32 *w, u32 ms)
{
    if (wait_low(n ? 1 : 4, ms))
        return -1;
    *w = REG16(port[n]);
    return 0;
}

static void probe(int n)
{
    u32 *v = dsp_values[n], i, t, w;
    u32 count = sizeof program / sizeof program[0];

    v[6] = LOOPS;
    REG32(LATCH) &= ~(4u << n);
    delay_ms(20);
    REG32(LATCH) |= 4u << n;
    delay_ms(20);
    /* The boot ROM's download: a word that does not count, the number
     * of instructions, the host port's mode (16 bits, with waits), and
     * the instructions, low half first. */
    v[0] = 0xe1;
    if (put(n, 0) || put(n, (u16)count) || put(n, 0x0c00))
        return;
    v[0] = 0xe2;
    for (i = 0; i < count; i++)
        if (put(n, (u16)program[i]) || put(n, (u16)(program[i] >> 16)))
            return;
    v[0] = 0xe3;
    if (get(n, &v[1], 500) || get(n, &v[2], 500) || get(n, &v[3], 500))
        return;
    v[0] = 0xe4;
    if (put(n, LOOPS))
        return;
    t = counts();
    if (get(n, &w, 20000) || w != LOOPS)
        return;
    v[4] = counts() - t;
    v[0] = 0xe5;
    if (get(n, &w, 1000) || w != 0x180)
        return;
    v[0] = 0xe6;
    if (put(n, LOOPS))
        return;
    t = counts();
    if (get(n, &w, 20000) || w != LOOPS)
        return;
    v[5] = counts() - t;
    v[0] = 1;
}

void dsp_probe(void)
{
    int n;
    for (n = 0; n < 2; n++) {
        probe(n);
        REG32(LATCH) &= ~(4u << n);
    }
}
