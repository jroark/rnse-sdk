/* A second row of measurements for a unit on a bench, small enough to
 * come from an SD card (examples/sdboot) and with no hands wanted:
 * what the first row (examples/suite) left open about time.
 *
 * Records as the suite's, on CAN 0x7F8: test, item, a value of 32
 * bits and one of 16.
 *
 *   20  the display: vertical syncs counted in two seconds (item 0)
 *       and the microseconds from one to the next, eight times (1..8)
 *   21  the cards, each slot (items +0 for the first, +0x10 for the
 *       second): what sd_init said and its milliseconds (0), the last
 *       command and the processor's answer (1), then microseconds:
 *       one sector (2), 64 sectors one after the other (3), 64 sectors
 *       1000 apart (4, within the first 15 MB), with the number that
 *       could be read
 *   22  the bus, microseconds for 8192 accesses each: display memory
 *       read (0) and written (1), a register of the graphics chip (2),
 *       of the CAN controller (3), the card processors' status port
 *       (4), the drive's status (5), main memory written uncached (6),
 *       and a port of the companion chip (7)
 *
 * Test 0xff closes the run and the unit restarts.  The 192 only. */
#include "rnse.h"

#define REG8(a) (*(volatile u8 *)(a))
#define REG16(a) (*(volatile u16 *)(a))
#define REG32(a) (*(volatile u32 *)(a))

#define CORAL_IST 0xb9fc0020
#define VSYNC 4u

static int s, row;
static u8 sector[512];
static u32 scratch[8192];

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

static void show(const char *name, u32 v)
{
    char text[64], *p = text;
    int y = (2 + 9 * row) * s;
    while (*name)
        *p++ = *name++;
    *p++ = ' ';
    p = put_dec(p, v);
    *p = 0;
    screen_fill(0, y, screen_width, 8 * s, 0);
    screen_text(2 * s, y, text, 1, s);
    console_puts(text);
    console_puts("\n");
    if (++row >= 24)
        row = 1;
}

static void rec(u8 test, u8 item, u32 value, u32 second)
{
    u8 d[8] = { test, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, (u8)(second >> 8), (u8)second };
    can_send_for(0x7f8, d, 8, 3000);
}

/* The next vertical sync, or 0 when none comes in 100 ms. */
static u32 next_sync(void)
{
    u32 start = micros();
    REG32(CORAL_IST) = ~VSYNC;
    while (!(REG32(CORAL_IST) & VSYNC))
        if (micros() - start > 100000)
            return 0;
    return micros();
}

static void display(void)
{
    u32 start, n = 0, t, u, i;
    rec(20, 0xfe, millis(), 0);
    start = millis();
    REG32(CORAL_IST) = ~VSYNC;
    while (millis() - start < 2000)
        if (REG32(CORAL_IST) & VSYNC) {
            REG32(CORAL_IST) = ~VSYNC;
            n++;
        }
    rec(20, 0, n, 0);
    show("DISPLAY: SYNCS IN 2 S", n);
    t = next_sync();
    for (i = 1; i <= 8 && t; i++) {
        u = next_sync();
        rec(20, (u8)i, u ? u - t : 0, 0);
        if (i == 1)
            show("  US FROM ONE TO THE NEXT", u ? u - t : 0);
        t = u;
    }
}

static void cards(void)
{
    int slot;
    rec(21, 0xfe, millis(), 0);
    for (slot = 0; slot < 2; slot++) {
        u8 base = (u8)(slot * 0x10);
        u32 t = millis(), i, good;
        int r = sd_init(slot);
        rec(21, base, (u32)r, millis() - t);
        rec(21, (u8)(base + 1), (u32)sd_last[0] << 16 | sd_last[1], sd_last[2]);
        if (r) {
            show(slot ? "CARD 2: NOT STARTED, CODE" : "CARD 1: NOT STARTED, CODE", (u32)-r);
            show("  AT COMMAND", sd_last[0] & 0x3f);
            continue;
        }
        show(slot ? "CARD 2: STARTED IN MS" : "CARD 1: STARTED IN MS", millis() - t);
        t = micros();
        good = sd_read(0, sector) == 0;
        t = micros() - t;
        rec(21, (u8)(base + 2), t, good);
        show("  ONE SECTOR US", t);
        t = micros();
        for (i = 0, good = 0; i < 64; i++)
            good += sd_read(64 + i, sector) == 0;
        t = micros() - t;
        rec(21, (u8)(base + 3), t, good);
        show("  64 IN A ROW US", t);
        t = micros();
        for (i = 0, good = 0; i < 64; i++)
            good += sd_read((i * 1000) % 30000, sector) == 0;
        t = micros() - t;
        rec(21, (u8)(base + 4), t, good);
        show("  64 APART US", t);
    }
}

static u32 __attribute__((noinline)) read32(volatile u32 *p, u32 n, u32 step)
{
    u32 sum = 0;
    while (n--) {
        sum += *p;
        p += step;
    }
    return sum;
}

static u32 __attribute__((noinline)) read16(volatile u16 *p, u32 n)
{
    u32 sum = 0;
    while (n--)
        sum += *p;
    return sum;
}

static u32 __attribute__((noinline)) read8(volatile u8 *p, u32 n)
{
    u32 sum = 0;
    while (n--)
        sum += *p;
    return sum;
}

static void __attribute__((noinline)) write32(volatile u32 *p, u32 n, u32 v)
{
    while (n--)
        *p++ = v;
}

static void timed(u8 item, const char *name, u32 t, u32 sum)
{
    rec(22, item, t, sum);
    show(name, t);
}

static void bus(void)
{
    u32 t, sum;
    rec(22, 0xfe, millis(), 0);
    /* (Display memory beyond the picture: the second megabyte.) */
    t = micros();
    sum = read32((volatile u32 *)0xb8100000, 8192, 1);
    timed(0, "BUS: DISPLAY MEMORY READ US", micros() - t, sum);
    t = micros();
    write32((volatile u32 *)0xb8100000, 8192, 0);
    timed(1, "  DISPLAY MEMORY WRITTEN US", micros() - t, 0);
    t = micros();
    sum = read32((volatile u32 *)CORAL_IST, 8192, 0);
    timed(2, "  GRAPHICS REGISTER US", micros() - t, sum);
    t = micros();
    sum = read16((volatile u16 *)0xa7c00000, 8192);
    timed(3, "  CAN REGISTER US", micros() - t, sum);
    t = micros();
    sum = read8((volatile u8 *)0xb2400001, 8192);
    timed(4, "  CARD PROCESSORS' STATUS US", micros() - t, sum);
    t = micros();
    sum = read8((volatile u8 *)0xa790000d, 8192);
    timed(5, "  DRIVE STATUS US", micros() - t, sum);
    t = micros();
    write32((volatile u32 *)(((u32)scratch & 0x1fffffff) | 0xa0000000), 8192, 0);
    timed(6, "  MEMORY WRITTEN UNCACHED US", micros() - t, 0);
    t = micros();
    sum = read8((volatile u8 *)0xb2300001, 8192);
    timed(7, "  PORT US", micros() - t, sum);
}

int main(void)
{
    board_init();
    console_puts("\nrnse sdk: timing\n");
    if (screen_width != 400) {
        console_puts("the 192 only\n");
        restart();
    }
    s = 1;
    screen_palette(1, 255, 255, 255);
    can_init();
    show("RNSE SDK: TIMING.  RESULTS ON CAN 7F8, TEST", 20);
    display();
    bus();
    cards();
    rec(0xff, 0xff, millis(), 0);
    show("RESTARTING, MS", millis());
    delay_ms(3000);
    restart();
    return 0;
}
