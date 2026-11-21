/* The first program for a real 193 on a bench, the 193's "report" and
 * "suite" in one run: what the emulator of the 193 so far only infers
 * is read off the unit, and everything goes out on the CAN bus (for a
 * CANable or a logic analyser on the two wires), on the screen and on
 * the debug serial port.  Nothing is written anywhere else: not to the
 * flash, not to the battery-backed memory, not to a card; no updater is
 * ever started.  At the end the unit is started again (restart).  It
 * reads no key and moves nothing (the bench 193's first evening showed
 * the kit's faceplate code misreading the unit's keys): from the SD
 * loader (examples/sdload193) it needs no disc of its own, and from a
 * disc that disc is ejected in the stock software's half minute after a
 * restart.  Frames go out through the CAN controller as the stock
 * software left it.
 *
 * Records, as the suite's: frame 0x7F8, eight bytes
 *
 *   test, item, value (four bytes), a second value (two bytes)
 *
 * item 0xfe opens a test (value: the clock in ms), test 0xff closes the
 * run.  The tests, in order:
 *
 *   0x20  registers as the stock software left them (read before
 *         anything is set up): item n, value, the address's low half;
 *         0x21 item n the address itself; 0x22 the start state (0-3)
 *         and the loader's arguments (4-6)
 *    1    the processor's speed (as the suite)
 *   13    the flash: autoselect and the common flash interface query of
 *         each bank of 32 MiB (items 0x00.. bank 0, 0x80.. bank 1:
 *         +0 manufacturer, +1 device, +2 and +3 the words at 0x0e and
 *         0x0f, +4.. the 48 query words from 0x10), read as 32-bit
 *         words, one half from each chip of the pair.  Read only
 *   14    which 128 KiB blocks of the 4 MiB below the software are not
 *         blank: item n, CRC-32, and 1 if anything is not 0xff
 *    2    the A/D converter's eight inputs
 *    3    ports: item 0x10 * block + register, the value
 *    7    the drive: its INQUIRY answer, 4 bytes a record
 *    8    the CAN controller's status (can_status)
 *   18    the dumps, each opened with item 0 (region, size) and closed
 *         with item 1 (region, CRC-32 of the whole region); the bytes
 *         go on frames 0x7F9: three bytes of address, five of data,
 *         the address being region * 0x400000 + offset.  A block of 256
 *         bytes all of one value (0x00 or 0xff) is sent as one frame
 *         0x7FA instead: three bytes of address, the value.  Regions:
 *         1 the battery-backed memory (256 KiB at 0x18000000), 2 the
 *         top 128 KiB of the flash (the parameter block and the unit's
 *         serial number), 0 the boot ROM (from flash address 0, up to
 *         the last byte below 1 MiB that is not 0xff).  Also: item 2,
 *         the number of 4-byte words of the memory that read the same
 *         at +0x40000 (256 KiB further on) as at +0, out of 0x10000
 *
 * Everything is read through P2 (no cache).  EXTRA_CFLAGS: -DHANDS=n. */
#include "rnse.h"

#ifndef HANDS
#define HANDS 30
#endif

struct reg {
    u32 addr;
    u8 size;
};

/* What the 193's stock software reads or writes itself (as report), and
 * the ports around the ones it is known to use. */
static const struct reg regs[] = {
    { 0xff000030, 4 }, { 0xff000040, 4 }, { 0xff000044, 4 }, /* PVR, CVR, PRR */
    { 0xff00001c, 4 },                                        /* CCR */
    { 0xffd80004, 1 }, { 0xffd80008, 4 }, { 0xffd80010, 2 },  /* TSTR, TCOR0, TCR0 */
    { 0xffd80020, 4 }, { 0xffd80028, 2 },                     /* TCOR2, TCR2 */
    { 0xffd81004, 1 }, { 0xffd81008, 4 }, { 0xffd81010, 2 },  /* the CAN timer */
    { 0xff802000, 4 },                                        /* interrupt pins */
    { 0xff804000, 4 }, { 0xff804004, 4 }, { 0xff804008, 4 }, { 0xff80400c, 4 },
    { 0xff804010, 4 }, { 0xff804014, 4 }, { 0xff804018, 4 }, { 0xff80401c, 4 },
    { 0xff804020, 4 }, { 0xff804024, 4 }, { 0xff804028, 4 }, { 0xff80402c, 4 },
    { 0xff804038, 4 }, { 0xff804108, 4 },                     /* the two mask registers */
    { 0xffe40000, 2 }, { 0xffe40030, 2 }, { 0xffe40034, 2 },  /* debug port */
    { 0xff926000, 2 }, { 0xff926004, 1 },                     /* faceplate port */
    { 0xffdf0004, 2 }, { 0xffdf0006, 2 },                     /* CAN bit timing */
    { 0xff909000, 4 }, { 0xff909004, 4 },                     /* port with the faceplate's reset */
    { 0xff93f004, 4 }, { 0xff93f008, 4 }, { 0xff93f00c, 4 },  /* port with the display motor */
    { 0xffc44008, 4 },                                        /* port with the watchdog strobe */
    { 0xff972000, 4 }, { 0xff984000, 4 },                     /* audio clock, sound interface */
};
#define NREGS (int)(sizeof regs / sizeof regs[0])
static u32 values[NREGS];

/* Ports read in test 3: the blocks the stock software uses. */
static const u32 port_blocks[] = { 0xff93e000, 0xff93f000, 0xff940000, 0xffc40000, 0xffc44000, 0xff909000 };

#define FLASH 0xa0000000u
#define BANK 0x02000000u
#define SRAM 0xb8000000u
#define SRAM_SIZE 0x40000u
#define ADC 0xff93c000u

static int s, row;
static u8 buffer[64 * 1024];

static char *put_hex(char *p, u32 v, int digits)
{
    while (digits--)
        *p++ = "0123456789ABCDEF"[v >> (4 * digits) & 15];
    return p;
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

static void show(const char *text)
{
    int y = (2 + 9 * row) * s;
    screen_fill(0, y, screen_width, 8 * s, 0);
    screen_text(2 * s, y, text, 1, s);
    console_puts(text);
    console_puts("\n");
    if (++row >= 24)
        row = 1;
}

static void status(const char *text)
{
    int y = screen_height - 12 * s;
    screen_fill(0, y - 2 * s, screen_width, 12 * s, text ? 2 : 0);
    if (text)
        screen_text(4 * s, y, text, 3, s);
}

/* Frames go out with 3 ms each at most: acknowledged by a CANable they
 * take some 1.3 ms, and with nobody on the bus they are on the wire at
 * least once for a logic analyser. */
/* Through the CAN controller as the stock software left it (mailbox 1,
 * no reset), as examples/probe193 found to work on the unit: the kit's
 * can_init left the bus silent there.  A counted wait, then taken back
 * if nobody acknowledged (it was on the wire for a logic analyser). */
#define CAN 0xffdf0000
#define CAN_MB1 (CAN + 0x100 + 0x20)
/* A pause after each frame that was acknowledged (-DPACE_US=n).  On
 * the bench the Mac's end lost some 45% of the frames whether they came
 * 1.2 or 5.8 ms apart (2026-11-20), so none by default; the dumps are
 * sent several times over instead (DUMP_REPEAT). */
#ifndef DUMP_REPEAT
#define DUMP_REPEAT 10
#endif
#ifndef PACE_US
#define PACE_US 0
#endif
static void pace(void)
{
#if PACE_US > 0
    u32 t = micros();
    while (micros() - t < PACE_US)
        ;
#endif
}
static void frame(u16 id, const u8 *d, int len)
{
    u32 n;
    int i;
    REG16(CAN_MB1 + 0x00) = (u16)(id << 2);
    for (i = 0; i < len; i++)
        REG8(CAN_MB1 + 0x08 + i) = d[i];
    REG16(CAN_MB1 + 0x10) = (u16)(len & 15);
    REG32(CAN + 0x20) = 0x00000002;
    for (n = 0; n < 400000; n++)
        if (REG16(CAN + 0x32) & 0x0002)
            break;
    if (REG16(CAN + 0x32) & 0x0002) {
        REG16(CAN + 0x32) = 0x0002;
        pace();
        return;
    }
    REG32(CAN + 0x28) = 0x00000002;
    for (n = 0; n < 400000; n++)
        if (!(REG16(CAN + 0x22) & 0x0002) || (REG16(CAN + 0x3a) & 0x0002))
            break;
    REG16(CAN + 0x3a) = 0x0002;
}

static void rec(u8 test, u8 item, u32 value, u32 second)
{
    u8 d[8] = { test, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, (u8)(second >> 8), (u8)second };
    frame(0x7f8, d, 8);
}

static void open(u8 test, const char *name)
{
    char text[64], *p = text;
    p = put_dec(p, test);
    *p++ = ' ';
    p = put_str(p, name);
    *p = 0;
    show(text);
    rec(test, 0xfe, millis(), 0);
}

static void result(const char *name, u32 v, int hex)
{
    char text[64], *p = text;
    p = put_str(p, "   ");
    p = put_str(p, name);
    *p++ = ' ';
    p = hex ? put_hex(p, v, 8) : put_dec(p, v);
    *p = 0;
    show(text);
}

static u32 crc32_add(u32 c, const volatile u8 *p, u32 n)
{
    c = ~c;
    while (n--) {
        int k;
        c ^= *p++;
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xedb88320 & (0 - (c & 1)));
    }
    return ~c;
}

static void registers(void)
{
    int i;
    open(0x20, "REGISTERS");
    for (i = 0; i < NREGS; i++) {
        rec(0x21, (u8)i, regs[i].addr, regs[i].size);
        rec(0x20, (u8)i, values[i], regs[i].addr & 0xffff);
    }
    for (i = 0; i < 4; i++)
        rec(0x22, (u8)i, start_state[i], 0);
    for (i = 0; i < 3; i++)
        rec(0x22, (u8)(4 + i), start_args[i], 0);
    result("PVR", values[0], 1);
    result("PRR", values[2], 1);
}

static u32 __attribute__((noinline)) spin(u32 n)
{
    u32 x = 0;
    while (n--)
        x = x * 3 + n;
    return x;
}

static u32 __attribute__((noinline)) add_up(const u32 *p, u32 words)
{
    u32 sum = 0;
    while (words--)
        sum += *p++;
    return sum;
}

static void speed(void)
{
    u32 t, i, sum;
    open(1, "PROCESSOR");
    t = micros();
    sum = spin(2000000);
    t = micros() - t;
    rec(1, 0, t, sum);
    result("LOOP US", t, 0);
    t = micros();
    for (i = 0; i < 32; i++)
        sum += add_up((const u32 *)buffer + i, sizeof buffer / 2 / 4);
    t = micros() - t;
    rec(1, 1, t, sum);
    result("CACHED US", t, 0);
    t = micros();
    sum += add_up((const u32 *)(((u32)buffer & 0x1fffffff) | 0xa0000000), sizeof buffer / 2 / 4);
    t = micros() - t;
    rec(1, 2, t, sum);
    result("UNCACHED US", t, 0);
}

void irq_disable(void);
void irq_enable(void);

/* Both chips of a pair get every command, one in each half of the word. */
#define BOTH(c) ((u32)(c) << 16 | (u32)(c))

static void flash_banks(void)
{
    u32 bank;
    open(13, "FLASH BANKS (READ ONLY)");
    for (bank = 0; bank < 2; bank++) {
        u32 base = FLASH + BANK * bank, i, id[4], cfi[48];
        /* Nothing may read this bank while it answers, and nothing does:
         * the program, its data and the vectors are in RAM.  Interrupts
         * off all the same, for the fraction of a millisecond. */
        irq_disable();
        REG32(base + 0x555 * 4) = BOTH(0xaa);
        REG32(base + 0x2aa * 4) = BOTH(0x55);
        REG32(base + 0x555 * 4) = BOTH(0x90);
        id[0] = REG32(base);
        id[1] = REG32(base + 1 * 4);
        id[2] = REG32(base + 0x0e * 4);
        id[3] = REG32(base + 0x0f * 4);
        REG32(base) = BOTH(0xf0);
        REG32(base + 0x55 * 4) = BOTH(0x98);
        for (i = 0; i < 48; i++)
            cfi[i] = REG32(base + (0x10 + i) * 4);
        REG32(base) = BOTH(0xf0);
        irq_enable();
        for (i = 0; i < 4; i++)
            rec(13, (u8)(0x80 * bank + i), id[i], 0);
        for (i = 0; i < 48; i++)
            rec(13, (u8)(0x80 * bank + 4 + i), cfi[i], 0);
        result("MANUFACTURER", id[0], 1);
        result("DEVICE", id[1], 1);
        result("QUERY", (cfi[0] & 0xff) << 16 | (cfi[1] & 0xff) << 8 | (cfi[2] & 0xff), 1);
    }
}

static void blank_map(void)
{
    u32 b;
    open(14, "FLASH BELOW THE SOFTWARE");
    for (b = 0; b < 32; b++) {
        const volatile u32 *w = (const volatile u32 *)(FLASH + b * 0x20000);
        u32 i, used = 0;
        for (i = 0; i < 0x20000 / 4; i++)
            if (w[i] != 0xffffffff) {
                used = 1;
                break;
            }
        rec(14, (u8)b, crc32_add(0, (const volatile u8 *)w, 0x20000), used);
    }
}

static void converter(void)
{
    int ch;
    open(2, "A/D CONVERTER");
    for (ch = 0; ch < 8; ch++) {
        int v = adc_read(ch);
        rec(2, (u8)ch, (u32)v, 0);
    }
    result("DISPLAY POSITION", (u32)display_position(), 0);
}

static void ports(void)
{
    u32 b, r;
    open(3, "PORTS");
    for (b = 0; b < sizeof port_blocks / sizeof port_blocks[0]; b++)
        for (r = 0; r < 4; r++)
            rec(3, (u8)(0x10 * b + r), REG32(port_blocks[b] + 4 * r), 0);
    result("DISPLAY PORT IN", REG32(0xff93f00c), 1);
}

static void drive(void)
{
    static const u8 inquiry[12] = { 0x12, 0, 0, 0, 96 };
    u32 i;
    int r;
    open(7, "DRIVE");
    for (i = 0; i < 96; i++)
        buffer[i] = 0;
    r = cd_command(inquiry, buffer, 96);
    rec(7, 0xfd, (u32)r, 0);
    for (i = 0; i < 96; i += 4)
        rec(7, (u8)(i / 4), (u32)buffer[i] << 24 | (u32)buffer[i + 1] << 16 | (u32)buffer[i + 2] << 8 | buffer[i + 3], 0);
    buffer[32] = 0;
    show((const char *)buffer + 8);
}

static void can_state(void)
{
    open(8, "CAN CONTROLLER");
    rec(8, 0, can_status(), 0);
    result("STATUS", can_status(), 1);
}

/* One region out on 0x7F9/0x7FA, read through P2. */
static void dump(u8 region, u32 from, u32 size, const char *name)
{
    u32 at, crc = 0;
    char text[48], *p = text;
    p = put_str(p, "SENDING ");
    p = put_str(p, name);
    *p = 0;
    status(text);
    rec(18, 0, region, size >> 8);
    for (at = 0; at < size; at += 256) {
        const volatile u8 *m = (const volatile u8 *)(from + at);
        u32 a = region * 0x400000u + at, i;
        u8 first = m[0];
        int same = first == 0 || first == 0xff;
        for (i = 1; same && i < 256; i++)
            same = m[i] == first;
        crc = crc32_add(crc, m, 256);
        if (same) {
            u8 d[4] = { (u8)(a >> 16), (u8)(a >> 8), (u8)a, first };
            frame(0x7fa, d, 4);
        } else {
            for (i = 0; i < 256; i += 5) {
                u8 d[8];
                u32 k;
                d[0] = (u8)((a + i) >> 16);
                d[1] = (u8)((a + i) >> 8);
                d[2] = (u8)(a + i);
                for (k = 0; k < 5; k++)
                    d[3 + k] = i + k < 256 ? m[i + k] : 0;
                frame(0x7f9, d, i + 5 <= 256 ? 8 : 3 + (int)(256 - i));
            }
        }
    }
    rec(18, 1, region, 0);
    rec(18, 3, crc, region);
    result(name, crc, 1);
    status(0);
}

static void dumps(void)
{
    u32 i, same = 0, rom;
    open(18, "DUMPS: MEMORY, PARAMETERS, BOOT ROM");
    for (i = 0; i < SRAM_SIZE / 4; i++)
        same += REG32(SRAM + 4 * i) == REG32(SRAM + SRAM_SIZE + 4 * i);
    rec(18, 2, same, 0);
    result("MEMORY WORDS SAME 256 KIB ON", same, 0);
    /* Several times over: a receiver that loses frames has each byte
     * again (the memory does not change while this runs, so the copies
     * are the same; first193.py puts them together within the run). */
    for (i = 0; i < DUMP_REPEAT; i++)
        dump(1, SRAM, SRAM_SIZE, "BATTERY-BACKED MEMORY");
    for (i = 0; i < 3; i++)
        dump(2, FLASH + 2 * BANK - 0x20000, 0x20000, "FLASH PARAMETERS");
    rom = 0x100000;
    while (rom > 0x1000 && ((const volatile u8 *)FLASH)[rom - 1] == 0xff)
        rom--;
    rom = (rom + 0xfff) & ~0xfffu;
    for (i = 0; i < 3; i++)
        dump(0, FLASH, rom, "BOOT ROM");
}

int main(void)
{
    int i;

    /* First, before anything is set up: the registers. */
    for (i = 0; i < NREGS; i++) {
        switch (regs[i].size) {
        case 1: values[i] = REG8(regs[i].addr); break;
        case 2: values[i] = REG16(regs[i].addr); break;
        default: values[i] = REG32(regs[i].addr); break;
        }
    }
    board_init();
    console_puts("\nrnse sdk: first193\n");
    s = screen_width / 400;
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 200, 40, 0);
    screen_palette(3, 255, 255, 255);
    REG32(CAN + 0x28) = 0xfffffffe; /* the stock software's queued frames taken back */
    show("RNSE SDK: FIRST 193.  RESULTS ON CAN 7F8-7FA");
    rec(0xf0, 0, millis(), (u32)screen_width);
    registers();
    speed();
    flash_banks();
    converter();
    ports();
    drive();
    can_state();
    dumps();
    blank_map();
    rec(0xff, 0xff, millis(), 0);
    show("DONE.  THE UNIT RESTARTS IN 5 S");
    delay_ms(5000);
    restart();
    return 0;
}
