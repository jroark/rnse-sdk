/* A first SD loader for the 193: starts RNSE.BIN from a card or the SD
 * bridge in either slot, so that programs need no new disc.  Written
 * after the bench 193's first evening (2026-11-20, see probe193), and
 * careful in the ways that evening asked for:
 *
 *   - it reports on the CAN bus through the controller as the stock
 *     software left it (mailbox 1, no reset): the kit's can_init did
 *     not work on the unit, and this did;
 *   - it starts the faceplate controller, which lights the screen
 *     (examples/panel193), and of its keys follows CD EJECT only: the
 *     disc comes out through the drive commands examples/eject193 used
 *     on the unit; the display key opens and closes the display (the
 *     kit's display driver, its lines read off the stock software,
 *     2026-11-21).  Switching the unit off and on also brings the stock
 *     software back for some 25 seconds before it starts this again.  (Before 2026-11-20 evening the kit's faceplate
 *     start drove the faceplate port's other lines low and set the drive
 *     ejecting over and over; fixed in src/hw193.c.);
 *   - it starts the unit again after a minute without a program.
 *
 * The card is read through the chip's SD host (src/hw193.c, sdhi_*),
 * one data line, polled.  Records on 0x7F8 (test, item, value, second):
 *
 *   0x70 0     alive (value: SR)
 *   0x55 s     board_init at its step s
 *   0x71 c     an SD command (item: its word; value: argument; second:
 *              the low half of the response); 0x72 c its result and
 *              the response's high half, when the result is not 0
 *   0x77 n     a pin register (n: 0..4 before, 0x10..0x14 after the
 *              stock software's settings, sdhi_pins): its address; 0x78
 *              n its value
 *   0x73 slot  sd_init's result for that slot
 *   0x74 r     sd_load's result; value: the file's size; second: the
 *              slot, plus 0x10 for LINKBENC.BIN
 *   0x75 0     CRC-32 of what was loaded, then the program is started
 *   0x76 n     the n-th round without a program
 *   0x79 0     CD EJECT pressed: the eject's result (0 done)
 *   0xff 0xff  no program in a minute: the unit starts again */
#include "rnse.h"

#define CAN 0xffdf0000
#define CAN_MB1 (CAN + 0x100 + 0x20)
#define WATCHDOG 0xffc44008
#define BUFFER ((u8 *)0x8c000000) /* RAM well away from this program */
#define MAX_PROGRAM 0x100000

static int ticking;
static u32 strobe;

static void feed(void)
{
    if (!ticking) {
        strobe ^= 1;
        REG32(WATCHDOG) = (REG32(WATCHDOG) & ~1u) | strobe;
    }
}

/* A frame on mailbox 1 with a counted wait. */
static void say(u8 test, u8 item, u32 value, u32 second)
{
    u8 d[8] = { test, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, (u8)(second >> 8), (u8)second };
    u32 n;
    int i;
    REG16(CAN_MB1 + 0x00) = (u16)(0x7f8 << 2);
    for (i = 0; i < 8; i++)
        REG8(CAN_MB1 + 0x08 + i) = d[i];
    REG16(CAN_MB1 + 0x10) = 8;
    REG32(CAN + 0x20) = 0x00000002;
    feed();
    for (n = 0; n < 400000; n++)
        if (REG16(CAN + 0x32) & 0x0002)
            break;
    if (REG16(CAN + 0x32) & 0x0002) {
        REG16(CAN + 0x32) = 0x0002;
    } else {
        REG32(CAN + 0x28) = 0x00000002;
        for (n = 0; n < 400000; n++)
            if (!(REG16(CAN + 0x22) & 0x0002) || (REG16(CAN + 0x3a) & 0x0002))
                break;
        REG16(CAN + 0x3a) = 0x0002;
    }
    feed();
}

static void step(int s)
{
    say(0x55, (u8)s, 0, 0);
}

/* The drive's status with a counted wait for the bits to clear. */
static int clear(u8 bits)
{
    u32 n;
    for (n = 0; n < 20000000; n++)
        if (!(cd_alt_status() & bits))
            return 0;
    return -1;
}

/* One packet command without data, as examples/probe193 and eject193
 * sent it on the unit. */
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

/* The faceplate answered (and with it the screen's lighting), the
 * display key and CD EJECT followed; called wherever this waits,
 * sd_load's reads too. */
static void service(void)
{
    static const u8 allow_removal[12] = { 0x1e, 0, 0, 0, 0 };
    static const u8 eject[12] = { 0x1b, 1, 0, 0, 2 };
    static int was;
    int down, r;
    panel_poll();
    display_follow_key();
    down = panel_key(KEY_CD_EJECT);
    if (down && !was) {
        r = packet(allow_removal);
        if (r == 0)
            r = packet(eject);
        say(0x79, 0, (u32)r, 0);
    }
    was = down;
}

static void wait_ms(u32 ms)
{
    u32 end = millis() + ms;
    while ((s32)(millis() - end) < 0) {
        service();
        cpu_idle();
    }
}

static void sd_said(int cmd, u32 arg, u32 response, int result)
{
    if (cmd >= 0x100) {
        /* A pin register before (0x100 + n) or after (0x110 + n) the
         * stock software's settings: the address, then the value. */
        say(0x77, (u8)cmd, arg, 0);
        say(0x78, (u8)cmd, response, 0);
        return;
    }
    say(0x71, (u8)cmd, arg, response & 0xffff);
    if (result)
        say(0x72, (u8)cmd, (u32)result, response >> 16);
}

static u32 crc32(const u8 *p, u32 n)
{
    u32 c = 0xffffffff;
    while (n--) {
        int k;
        c ^= *p++;
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xedb88320 & (0 - (c & 1)));
    }
    return ~c;
}

int main(void)
{
    u32 sr, size = 0, round;
    int slot, r;

    __asm__ volatile("stc sr, %0" : "=r"(sr));
    /* What the stock software had queued on the bus is taken back. */
    REG32(CAN + 0x28) = 0xfffffffe;
    say(0x70, 0, sr, 0);

    board_init_step = step;
    board_init();
    board_init_step = 0;
    ticking = 1;
    say(0x55, 0xff, millis(), 0);
    console_puts("\nrnse sdk: sdload193\n");
    screen_palette(1, 255, 255, 255);
    screen_text(16, 16, "SD LOADER 193: LINKBENC.BIN OR RNSE.BIN", 1, 2);
    screen_text(16, 48, "SLOT 2, THEN SLOT 1.  CD EJECT WORKS", 1, 2);
    panel_init();
    sd_idle = service;
    wait_ms(500); /* the faceplate's start and the lighting */

    sdhi_trace = sd_said;
    for (round = 0; round < 30; round++) {
        for (slot = 0; slot < 2; slot++) {
            r = sd_init(slot);
            say(0x73, (u8)slot, (u32)r, 0);
            if (r)
                continue;
            sdhi_trace = 0; /* the reads themselves are not told */
            /* The SD bridge's pushed program first (link.py serve puts
             * it there as LINKBENC.BIN), then a card's RNSE.BIN. */
            r = sd_load("LINKBENCBIN", BUFFER, MAX_PROGRAM, &size);
            say(0x74, (u8)r, size, (u16)(slot | 0x10));
            if (r) {
                r = sd_load("RNSE    BIN", BUFFER, MAX_PROGRAM, &size);
                say(0x74, (u8)r, size, (u16)slot);
            }
            if (r == 0) {
                say(0x75, 0, crc32(BUFFER, size), 0);
                screen_text(16, 80, "STARTING THE PROGRAM", 1, 2);
                wait_ms(100);
                r = run_image(BUFFER, size);
                say(0x75, 1, (u32)r, 0); /* only when it was no program */
            }
            sdhi_trace = sd_said;
        }
        say(0x76, (u8)round, millis(), 0);
        wait_ms(2000);
    }
    say(0xff, 0xff, millis(), 0);
    restart();
    return 0;
}
