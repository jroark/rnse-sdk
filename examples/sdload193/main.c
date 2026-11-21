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
 *     on the unit.  The display key is not followed (which motor line
 *     opens the 193's display is still assumed).  The disc stays in
 *     otherwise; switching the unit off and on brings the stock
 *     software back, which ejects the disc for some 25 seconds before it
 *     starts this again.  (Before 2026-11-20 evening the kit's faceplate
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
 *   0xe0 b     before board_init, the display unit as the stock
 *              software left it: block b (0 0xfff80000, 1 0xfff90000),
 *              the word, its offset; 0xe1 b the same after 8 s of the
 *              pattern below
 *   0x7b p     a plane the stock software shows: p its mode (second:
 *              its position), 0x10+p its surface (second: its stride),
 *              0x20+p its width and height; a pattern is written into
 *              it and kept 8 s (the kit's own set-up comes after)
 *   0x7a 0     the kit's own display set-up done
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

/* The faceplate answered (and with it the screen's lighting), and CD
 * EJECT followed; called wherever this waits, sd_load's reads too. */
static void service(void)
{
    static const u8 allow_removal[12] = { 0x1e, 0, 0, 0, 0 };
    static const u8 eject[12] = { 0x1b, 1, 0, 0, 2 };
    static int was;
    int down, r;
    panel_poll();
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

/* ---- the display unit as the stock software leaves it -------------- */

#define DU 0xfff80000

/* Words of the display unit, each a record (test, 0 or 1 for the block
 * at 0xfff80000 or 0xfff90000, the word, its offset), a little room
 * on the bus between them for the logger. */
static void du_words(u8 test, u8 block, u32 from, u32 to)
{
    u32 o, n;
    for (o = from; o <= to; o += 4) {
        say(test, block, REG32((block ? 0xfff90000 : DU) + o), o);
        for (n = 0; n < 4000; n++)
            __asm__ volatile("nop");
    }
}

static void du_dump(u8 test)
{
    u32 p;
    du_words(test, 0, 0x000, 0x0fc);          /* the control registers */
    for (p = 1; p <= 8; p++)                   /* the planes */
        du_words(test, 0, 0x100 * p, 0x100 * p + 0x50);
    du_words(test, 0, 0xa100, 0xa150);         /* two blocks the OS clears */
    du_words(test, 0, 0xa200, 0xa250);
    for (p = 1; p <= 4; p++)                   /* the palettes' first words */
        du_words(test, 0, 0x1000 * p, 0x1000 * p + 0x0c);
    du_words(test, 1, 0x00, 0x10);
}

/* Each plane the display shows (DPPR, +0x18: a nibble a position, bit
 * 3 shown, the low bits the plane less one): its mode, stride, size
 * and surface said (0x7b), and a pattern written into the middle of
 * its surface through the uncached window, 8 or 16 bits a pixel by
 * the mode's low bits, as the emulated stock software sets them. */
static void stock_planes(void)
{
    u32 dppr = REG32(DU + 0x18), pos;
    for (pos = 0; pos < 8; pos++) {
        u32 nib = dppr >> (4 * pos) & 0xf, p, mode, stride, w, h, addr, x, y, bpp;
        if (!(nib & 8))
            continue;
        p = (nib & 7) + 1;
        mode = REG32(DU + 0x100 * p);
        stride = REG32(DU + 0x100 * p + 0x04) & 0xfff;
        w = REG32(DU + 0x100 * p + 0x10) & 0xfff;
        h = REG32(DU + 0x100 * p + 0x14) & 0xfff;
        addr = REG32(DU + 0x100 * p + 0x20);
        say(0x7b, (u8)p, mode, (u16)pos);
        say(0x7b, (u8)(0x10 + p), addr, (u16)stride);
        say(0x7b, (u8)(0x20 + p), w << 16 | h, 0);
        bpp = (mode & 3) ? 2 : 1;
        if (addr < 0x08000000 || addr >= 0x10000000 || stride == 0 || stride > 2048 || h == 0)
            continue;
        for (y = h / 4; y < h * 3 / 4 && y < 600; y++) {
            for (x = (w ? w : stride) / 4; x < (w ? w : stride) * 3 / 4 && x < stride; x++) {
                u32 a = (addr | 0xa0000000) + (y * stride + x) * bpp;
                u32 band = x * 4 / (w ? w : stride);
                if (bpp == 2) {
                    static const u16 c[4] = { 0xf800, 0x07e0, 0x001f, 0xffff };
                    REG16(a) = (y & 16) ? c[band & 3] : 0xffff;
                } else {
                    REG8(a) = (u8)((y & 16) ? band * 60 + 15 : 255);
                }
            }
            if (!(y & 31))
                service();
        }
    }
}

int main(void)
{
    u32 sr, size = 0, round;
    int slot, r;

    __asm__ volatile("stc sr, %0" : "=r"(sr));
    /* What the stock software had queued on the bus is taken back. */
    REG32(CAN + 0x28) = 0xfffffffe;
    say(0x70, 0, sr, 0);
    /* The display unit as the stock software left it, before anything
     * here touches it (on the bench 193 nothing the kit drew showed:
     * 2026-11-20). */
    du_dump(0xe0);

    board_init_step = step;
    board_init_display = 0; /* the stock software's picture kept for now */
    board_init();
    board_init_step = 0;
    ticking = 1;
    say(0x55, 0xff, millis(), 0);
    console_puts("\nrnse sdk: sdload193\n");
    panel_init();
    sd_idle = service;
    /* Into the stock software's own planes, as it set them up. */
    stock_planes();
    wait_ms(8000);
    du_dump(0xe1);
    /* Then the kit's own picture, as before. */
    screen_init();
    say(0x7a, 0, millis(), 0);
    screen_palette(1, 255, 255, 255);
    screen_text(16, 16, "SD LOADER 193: LINKBENC.BIN OR RNSE.BIN", 1, 2);
    screen_text(16, 48, "SLOT 2, THEN SLOT 1.  CD EJECT WORKS", 1, 2);
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
