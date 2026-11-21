/* The smallest first step on a real 193: where does a program of the
 * kit stop?  Written after examples/first193 stopped on a unit before it
 * showed anything and kept the disc in the drive.
 *
 * Everything that can go wrong comes after three things that cannot
 * depend on the rest:
 *
 *   1. the disc is ejected, through the drive interface as the stock
 *      software left it, with counted waits (no timer runs yet: millis
 *      and micros start in board_init).  From here on nothing can keep
 *      the disc in; a unit that stops is switched off and on;
 *   2. the screen goes dark: one write to the display unit's plane
 *      priority register, all planes off;
 *   3. CAN frames, through the controller as the stock software left it
 *      (mailbox 1, no reset): one before and one after every step.
 *
 * Records on 0x7F8, as the suite's (test, item, value, second value):
 *
 *   0x50 0     alive (value: SR, second: the first argument)
 *   0x51 0     the eject: 0 done, 1..3 the step that timed out, 0x100+
 *              the drive's error register
 *   0x52 0     the screen darkened
 *   0x53 n     about to read register n (value: its address)
 *   0x54 n     register n read (value: what it read)
 *   0x55 s     board_init at its step s (rnse.h)
 *   0x56 0     board_init done; 1 the faceplate online (value: ms)
 *   0x57 0     keys changed (value: the first 32)
 *   0x58 0     every second: ms since board_init
 *   0x60 n     DMA channel n as the stock software left it, read before
 *              anything else and then stopped: item 8n+0 status (+0x34),
 *              +1 control (+0x2c), +2 mode (+0x28), +3 source, +4
 *              destination, +5 count; 0x61 0 the common block (+0x10,
 *              +0x18)
 *   0x62 s     the canary (32 KiB below the stack, filled after the DMA
 *              stop) checked at stage s: value 0 untouched, else the
 *              first changed address, second value its low half
 *   0x5e 0..3  an exception nobody expected: EXPEVT, INTEVT, the
 *              address of the instruction (SPC), the address it used
 *              (TEA, 0xff00000c); then the kit's handler, which stops
 *   0xff 0xff  the end, then restart
 *
 * After board_init the kit's own CAN code takes over (can_init).  For
 * 60 seconds the program then serves the keys (panel_service: display
 * key, CD EJECT) and shows them, then restarts the unit. */
#include "rnse.h"

#define CAN 0xffdf0000
#define CAN_MB1 (CAN + 0x100 + 0x20)
#define DU 0xfff80000

static int kit_can;

/* Until board_init's tick runs, nothing else serves the external
 * watchdog (crt0 masks the interrupts, so the stock software's tick is
 * gone too): its strobe, bit 0 of this port, toggled here as the stock
 * loader does it, while this program waits.  The disc is out before any
 * long wait. */
#define WATCHDOG 0xffc44008
static u32 strobe;
static void feed(void)
{
    if (!kit_can) {
        strobe ^= 1;
        REG32(WATCHDOG) = strobe;
    }
}

/* A frame on mailbox 1 with a counted wait: no timer is needed. */
static void say(u8 test, u8 item, u32 value, u32 second)
{
    u8 d[8] = { test, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, (u8)(second >> 8), (u8)second };
    u32 n;
    int i;
    if (kit_can) {
        can_send_for(0x7f8, d, 8, 3000);
        return;
    }
    REG16(CAN_MB1 + 0x00) = (u16)(0x7f8 << 2);
    for (i = 0; i < 8; i++)
        REG8(CAN_MB1 + 0x08 + i) = d[i];
    REG16(CAN_MB1 + 0x10) = 8;
    REG32(CAN + 0x20) = 0x00000002;
    feed();
    for (n = 0; n < 400000; n++)
        if (REG16(CAN + 0x32) & 0x0002)
            break;
    feed();
    if (REG16(CAN + 0x32) & 0x0002) {
        REG16(CAN + 0x32) = 0x0002;
    } else {
        /* Nobody acknowledged: it was on the wire for a logic
         * analyser; taken back so that the next one can go. */
        REG32(CAN + 0x28) = 0x00000002;
        for (n = 0; n < 400000; n++)
            if (!(REG16(CAN + 0x22) & 0x0002) || (REG16(CAN + 0x3a) & 0x0002))
                break;
        REG16(CAN + 0x3a) = 0x0002;
    }
}

/* The drive's status with a counted wait for the bits to clear. */
static int clear(u8 bits)
{
    u32 n;
    for (n = 0; n < 20000000; n++) {
        if (!(cd_alt_status() & bits))
            return 0;
        if ((n & 0x3ffff) == 0)
            feed();
    }
    return -1;
}

/* One packet command without data, as src/cd.c sends it. */
static int packet(const u8 *command)
{
    int i;
    if (clear(0x80))
        return 1;
    cd_reg_write(6, 0xa0);
    if (clear(0x88))
        return 2;
    cd_reg_write(1, 0x00);
    cd_reg_write(4, 0x00);
    cd_reg_write(5, 0x08);
    cd_reg_write(7, 0xa0);
    cd_alt_status();
    if (clear(0x80))
        return 3;
    if (!(cd_alt_status() & 0x08))
        return 0x100 | cd_reg_read(1);
    for (i = 0; i < 12; i += 2)
        cd_data_write((u16)(command[i] << 8 | command[i + 1]));
    cd_alt_status();
    if (clear(0x80))
        return 4;
    if (cd_reg_read(7) & 0x01)
        return 0x100 | cd_reg_read(1);
    return 0;
}

extern char _stack_top[];
#define CANARY_WORDS (32768 / 4)
static volatile u32 *canary;

static void check(u8 stage)
{
    u32 i;
    for (i = 0; i < CANARY_WORDS; i++)
        if (canary[i] != (0x5a5aa5a5u ^ i)) {
            say(0x62, stage, (u32)&canary[i], canary[i]);
            return;
        }
    say(0x62, stage, 0, 0);
}

static void step(int s)
{
    say(0x55, (u8)s, 0, 0);
    if (s == 9 || s == 91 || s == 92)
        check((u8)s);
}

static void trapped(u32 expevt, u32 intevt, u32 spc)
{
    kit_can = 0; /* no timer runs with SR.BL set: the counted waits */
    say(0x5e, 0, expevt, 0);
    say(0x5e, 1, intevt, 0);
    say(0x5e, 2, spc, 0);
    say(0x5e, 3, REG32(0xff00000c), 0);
}

/* The registers first193 read before anything else: one of them is the
 * first suspect. */
static const struct {
    u32 addr;
    u8 size;
} regs[] = {
    { 0xff000030, 4 }, { 0xff000040, 4 }, { 0xff000044, 4 }, { 0xff00001c, 4 },
    { 0xffd80004, 1 }, { 0xffd80008, 4 }, { 0xffd80010, 2 }, { 0xffd80020, 4 }, { 0xffd80028, 2 },
    { 0xffd81004, 1 }, { 0xffd81008, 4 }, { 0xffd81010, 2 },
    { 0xff802000, 4 }, { 0xff804000, 4 }, { 0xff804004, 4 }, { 0xff804008, 4 }, { 0xff80400c, 4 },
    { 0xff804010, 4 }, { 0xff804014, 4 }, { 0xff804018, 4 }, { 0xff80401c, 4 },
    { 0xff804020, 4 }, { 0xff804024, 4 }, { 0xff804028, 4 }, { 0xff80402c, 4 },
    { 0xff804038, 4 }, { 0xff804108, 4 },
    { 0xffe40000, 2 }, { 0xffe40030, 2 }, { 0xffe40034, 2 },
    { 0xff926000, 2 }, { 0xff926004, 1 }, { 0xffdf0004, 2 }, { 0xffdf0006, 2 },
    { 0xff909000, 4 }, { 0xff909004, 4 }, { 0xff93f004, 4 }, { 0xff93f008, 4 }, { 0xff93f00c, 4 },
    { 0xffc44008, 4 }, { 0xff972000, 4 }, { 0xff984000, 4 },
    /* The address mode: MMUCR, and PASCR (SH-4A: 29- or 32-bit
     * physical addresses, the PMB). */
    { 0xff000010, 4 }, { 0xff000070, 4 },
};

int main(void)
{
    static const u8 allow_removal[12] = { 0x1e, 0, 0, 0, 0 };
    static const u8 eject[12] = { 0x1b, 1, 0, 0, 2 };
    u32 sr, i, end, next, keys = 0xffffffff;
    int r;

    static u32 dma[8][6], common[2];
    __asm__ volatile("stc sr, %0" : "=r"(sr));
    unexpected_hook = trapped;
    /* 0: every DMA channel read, then stopped, before anything else. */
    for (i = 0; i < 8; i++) {
        u32 ch = 0xff801000 + 0x80 * i;
        dma[i][0] = REG32(ch + 0x34);
        dma[i][1] = REG32(ch + 0x2c);
        dma[i][2] = REG32(ch + 0x28);
        dma[i][3] = REG32(ch + 0x00);
        dma[i][4] = REG32(ch + 0x04);
        dma[i][5] = REG32(ch + 0x08);
        REG32(ch + 0x30) = 1;
    }
    common[0] = REG32(0xff801410);
    common[1] = REG32(0xff801418);
    REG32(0xff801418) = 0;
    canary = (volatile u32 *)((u32)_stack_top - 0x4000 - 32768);
    for (i = 0; i < CANARY_WORDS; i++)
        canary[i] = 0x5a5aa5a5u ^ i;
    /* What the stock software had queued on the bus is taken back. */
    REG32(CAN + 0x28) = 0xfffffffe;
    say(0x50, 0, sr, start_args[0]);
    say(0x50, 1, 5, 0); /* this probe's version */

    for (i = 0; i < 8; i++) {
        int k;
        for (k = 0; k < 6; k++)
            say(0x60, (u8)(8 * i + k), dma[i][k], 0);
    }
    say(0x61, 0, common[0], (u16)common[1]);
    check(0);

    /* 1: the disc out. */
    r = packet(allow_removal);
    if (r == 0)
        r = packet(eject);
    say(0x51, 0, (u32)r, 0);

    /* 2: the screen dark (no plane shown). */
    REG32(DU + 0x18) = 0x76543210;
    say(0x52, 0, REG32(DU + 0x18), 0);

    /* 3: the registers, one at a time. */
    for (i = 0; i < sizeof regs / sizeof regs[0]; i++) {
        u32 v;
        say(0x53, (u8)i, regs[i].addr, regs[i].size);
        switch (regs[i].size) {
        case 1: v = REG8(regs[i].addr); break;
        case 2: v = REG16(regs[i].addr); break;
        default: v = REG32(regs[i].addr); break;
        }
        say(0x54, (u8)i, v, 0);
    }

#ifdef TRAPTEST
    /* For the emulator only: an exception, to see the hook work. */
    __asm__ volatile("trapa #0x10");
#endif
    /* 4: the kit's bring-up, step by step (it serves the watchdog from
     * its own tick once that runs). */
    board_init_step = step;
    board_init();
    board_init_step = 0;
    say(0x56, 0, millis(), 0);
    check(10);

    can_init();
    kit_can = 1;
    say(0x56, 2, can_status(), 0);
    console_puts("\nrnse sdk: probe193\n");
    screen_palette(1, 255, 255, 255);
    screen_text(16, 16, "PROBE 193: KEYS SHOWN, CD EJECT WORKS", 1, 2);
    panel_init();
    end = millis() + 3000;
    while (!panel_online() && (s32)(millis() - end) < 0)
        panel_poll();
    say(0x56, 1, panel_online() ? millis() : 0xffffffff, 0);

    /* 5: a minute of keys, then the unit starts again. */
    display_idle = panel_service;
    end = millis() + 60000;
    next = 0;
    while ((s32)(millis() - end) < 0) {
        u32 now = 0;
        int k;
        panel_service();
        for (k = 0; k < 32; k++)
            now |= (u32)(panel_key(k) != 0) << k;
        if (now != keys) {
            keys = now;
            say(0x57, 0, keys, 0);
            screen_fill(16, 60, 400, 20, 0);
            screen_text(16, 60, now ? "KEY DOWN" : "NO KEY", 1, 2);
        }
        if ((s32)(millis() - next) >= 0) {
            next = millis() + 1000;
            say(0x58, 0, millis(), 0);
        }
        cpu_idle();
    }
    say(0xff, 0xff, millis(), 0);
    restart();
    return 0;
}
