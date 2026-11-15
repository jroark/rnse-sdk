/* A row of measurements in one run, for a unit on a bench with a logic
 * analyser on its CAN wires: what the emulator still has to guess is
 * asked of the hardware, one question after the other, and every
 * answer goes out on the bus (and to the screen and the debug serial
 * port), so that one disc and one capture settle many things.
 *
 * Nobody has to acknowledge: every record is given 3 ms on the bus.
 * A record is a frame 0x7F8:
 *
 *   test, item, value (four bytes), a second value (two bytes)
 *
 * The tests, in order.  None of them writes to the flash or to a card.
 *
 *    1  the processor's speed: microseconds for a loop of 2 000 000
 *       rounds (item 0), for adding up 1 MiB of words through the
 *       cache (1) and 32 KiB of them past it (2)
 *    2  the A/D converter's seven inputs (192)
 *    3  the eight ports, data and direction, the processor's own two
 *       ports and the ten registers at 0xb6000000 (192)
 *    4  the clock chip: its 64 Hz counter against the timer, counts in
 *       a second
 *    5  the faceplate controller: milliseconds until it has its
 *       settings
 *    6  the radio processor's serial line, listened to for three
 *       seconds without a word said: the bytes that came (192)
 *    7  the drive: its name (INQUIRY, items 0..8), the disc's size (9),
 *       microseconds for 64 sectors at the start (10), in the middle
 *       (11), near the end (12) and at the start again (13), and for
 *       256 in a row (14)
 *    8  the CAN controller's status and error counters (192)
 *    9  thirty seconds for hands: every change of the keys (item 0:
 *       four bytes of them and two more) and the two knobs' counts
 *       (1, 2).  RETURN ends it early.
 *   10  the watchdog: its strobe is left alone and a record follows
 *       every 10 ms with the milliseconds since, until the unit is
 *       reset.  Without a reset in ten seconds the unit is restarted.
 *
 * Record 0 opens a test (value = the clock in ms), record 0xff with
 * test 0xff closes the run.  The unit starts its software again at the
 * end and, with the disc still in the drive, this program after it.
 *
 * EXTRA_CFLAGS: -DHANDS=n seconds for test 9 (default 30). */
#include "rnse.h"

#ifndef HANDS
#define HANDS 30
#endif

static int s, row;
/* (The 192 has 128 KiB for program, data and stack.) */
static u8 buffer[32 * 2048];

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
    if (++row >= 25)
        row = 1;
}

static void rec(u8 test, u8 item, u32 value, u32 second)
{
    u8 d[8] = { test, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, (u8)(second >> 8), (u8)second };
    can_send_for(0x7f8, d, 8, 3000);
}

static void open(u8 test, const char *name)
{
    char text[48], *p = text;
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
    u32 t, i, sum = 0;
    open(1, "PROCESSOR");
    t = micros();
    sum = spin(2000000);
    t = micros() - t;
    rec(1, 0, t, sum);
    result("LOOP US", t, 0);
    t = micros();
    /* (From a different word each time, or the compiler adds up once.) */
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

#define ADC 0xa7d00000

static void converter(void)
{
    int ch;
    open(2, "A/D CONVERTER");
    for (ch = 0; ch < 7; ch++) {
        u32 start = millis(), v = 0xffff;
        REG16(ADC + 8) = (u16)(0x2000 | ch);
        while (millis() - start < 50) {
            if (REG16(ADC + 8) & 0x8000) {
                v = REG16(ADC + (ch < 4 ? 2 * ch : 2 * (ch - 4))) >> 6;
                REG16(ADC + 8) = 0;
                break;
            }
        }
        rec(2, (u8)ch, v, 0);
        result("INPUT", v, 0);
    }
}

static void ports(void)
{
    int i;
    char text[64], *p = text;
    open(3, "PORTS");
    for (i = 0; i < 8; i++) {
        u32 base = 0xb2300000 + 0x100000 * (u32)i;
        u32 dir = REG8(base), data = REG8(base + 1);
        rec(3, (u8)i, data, dir);
        p = put_hex(p, data, 2);
        *p++ = '/';
        p = put_hex(p, dir, 2);
        *p++ = ' ';
    }
    *p = 0;
    show(text);
    rec(3, 8, REG16(0xff800030), REG32(0xff80002c) >> 16);
    rec(3, 9, REG16(0xff800044), REG32(0xff800040) & 0xffff);
    for (i = 0; i < 10; i++)
        rec(3, (u8)(16 + i), REG32(0xb6000000 + 4 * (u32)i), 0);
    result("LATCH", REG32(0xb6000004), 1);
}

static void clock_chip(void)
{
    u32 start, last, n = 0;
    open(4, "CLOCK CHIP");
    start = micros();
    last = REG8(0xffc80000) & 0x7f;
    while (micros() - start < 1000000) {
        u32 now = REG8(0xffc80000) & 0x7f;
        if (now != last) {
            n++;
            last = now;
        }
    }
    rec(4, 0, n, REG8(0xffc8003c));
    result("COUNTS IN A SECOND", n, 0);
}

static void faceplate(void)
{
    u32 start = millis(), t;
    open(5, "FACEPLATE");
    panel_init();
    while (!panel_online() && millis() - start < 3000) {
        panel_poll();
        cpu_idle();
    }
    t = panel_online() ? millis() - start : 0xffffffff;
    rec(5, 0, t, 0);
    result("SETTINGS TAKEN AFTER MS", t, 0);
}

#define UART1 0xa7a10000

static void radio_line(void)
{
    u32 start = millis(), n = 0, word = 0;
    open(6, "RADIO PROCESSOR LINE");
    while (millis() - start < 3000 && n < 96) {
        if (REG16(UART1 + 0x08) & 0x0040) {
            word = word << 8 | (REG16(UART1 + 0x0a) & 0xff);
            if ((++n & 3) == 0)
                rec(6, (u8)(n >> 2), word, n);
        }
        panel_poll();
    }
    if (n & 3)
        rec(6, (u8)((n >> 2) + 1), word << (8 * (4 - (n & 3))), n);
    rec(6, 0, n, REG16(UART1 + 0x08));
    result("BYTES HEARD", n, 0);
}

static u32 timed_read(u32 sector, u32 count)
{
    u32 t = micros(), done = 0;
    while (done < count) {
        u32 n = count - done < 32 ? count - done : 32;
        if (cd_read(sector + done, n, buffer))
            return 0xffffffff;
        done += n;
    }
    return micros() - t;
}

static void drive(void)
{
    static const u8 inquiry[12] = { 0x12, 0, 0, 0, 36 };
    static const u8 capacity[12] = { 0x25 };
    u8 d[36];
    u32 size = 0, i, t;
    open(7, "DRIVE");
    cd_init();
    for (i = 0; i < sizeof d; i++)
        d[i] = 0;
    if (cd_command(inquiry, d, sizeof d) == 0) {
        char text[40], *p = text;
        for (i = 0; i < 9; i++)
            rec(7, (u8)i, (u32)d[4 * i] << 24 | (u32)d[4 * i + 1] << 16 | (u32)d[4 * i + 2] << 8 | d[4 * i + 3], 0);
        for (i = 8; i < 36; i++)
            *p++ = d[i] >= 0x20 && d[i] < 0x7f ? (char)(d[i] >= 'a' && d[i] <= 'z' ? d[i] - 32 : d[i]) : ' ';
        *p = 0;
        show(text);
    } else {
        rec(7, 0, 0xffffffff, 0);
        show("   NO ANSWER TO INQUIRY");
    }
    if (cd_wait_ready(20000)) {
        rec(7, 9, 0xffffffff, 0);
        show("   NOT READY");
        return;
    }
    if (cd_command(capacity, d, 8) == 0)
        size = ((u32)d[0] << 24 | (u32)d[1] << 16 | (u32)d[2] << 8 | d[3]) + 1;
    rec(7, 9, size, 0);
    result("SECTORS", size, 0);
    if (size < 1024)
        return;
    t = timed_read(16, 64);
    rec(7, 10, t, 0);
    result("64 AT THE START US", t, 0);
    t = timed_read(size / 2, 64);
    rec(7, 11, t, 0);
    result("64 IN THE MIDDLE US", t, 0);
    t = timed_read(size - 200, 64);
    rec(7, 12, t, 0);
    result("64 NEAR THE END US", t, 0);
    t = timed_read(16, 64);
    rec(7, 13, t, 0);
    result("64 AT THE START AGAIN US", t, 0);
    t = timed_read(80, 256);
    rec(7, 14, t, 0);
    result("256 IN A ROW US", t, 0);
}

static void can_state(void)
{
    open(8, "CAN CONTROLLER");
    if (screen_width == 400) {
        rec(8, 0, (u32)REG16(0xa7c00000) << 16 | REG16(0xa7c00002), REG16(0xa7c00008));
        rec(8, 1, (u32)REG16(0xa7c0000c) << 16 | REG16(0xa7c0000e), 0);
        result("STATUS", (u32)REG16(0xa7c00002) << 16 | REG16(0xa7c0000c), 1);
    }
}

static void hands(void)
{
    u32 start = millis(), keys = 0xffffffff, more = 0xffffffff;
    int volume = 0, select = 0, k, turned;
    open(9, "KEYS AND KNOBS: PRESS, TURN.  RETURN ENDS");
    while (millis() - start < HANDS * 1000u) {
        u32 now = 0, now2 = 0;
        panel_poll();
        for (k = 0; k < 32; k++)
            now |= (u32)(panel_key(k) != 0) << k;
        for (k = 32; k < 48; k++)
            now2 |= (u32)(panel_key(k) != 0) << (k - 32);
        if (now != keys || now2 != more) {
            keys = now;
            more = now2;
            rec(9, 0, keys, more);
            result("KEYS", keys, 1);
        }
        /* (panel_knob gives the steps since it was last asked.) */
        turned = panel_knob(KNOB_VOLUME);
        if (turned) {
            volume += turned;
            rec(9, 1, (u32)volume, 0);
            result("VOLUME KNOB", (u32)volume, 1);
        }
        turned = panel_knob(KNOB_SELECT);
        if (turned) {
            select += turned;
            rec(9, 2, (u32)select, 0);
            result("SELECT KNOB", (u32)select, 1);
        }
        if (panel_key(KEY_RETURN))
            break;
        cpu_idle();
    }
}

static void watchdog(void)
{
    u32 start, last = 0;
    open(10, "WATCHDOG: THE STROBE STOPS");
    delay_ms(200);
    start = millis();
    watchdog_hold = 1;
    while (millis() - start < 10000) {
        u32 t = millis() - start;
        if (t != last) {
            last = t;
            rec(10, 0, t, 0);
        }
    }
    watchdog_hold = 0;
    rec(10, 1, 0xffffffff, 0);
    show("   NO RESET IN TEN SECONDS");
}

int main(void)
{
    board_init();
    console_puts("\nrnse sdk: suite\n");
    s = screen_width / 400;
    screen_palette(1, 255, 255, 255);
    can_init();
    show("RNSE SDK: SUITE.  RESULTS ON CAN 7F8");
    speed();
    if (screen_width == 400) {
        converter();
        ports();
    }
    clock_chip();
    faceplate();
    if (screen_width == 400)
        radio_line();
    drive();
    can_state();
    hands();
    watchdog();
    rec(0xff, 0xff, millis(), 0);
    show("RESTARTING");
    delay_ms(1000);
    restart();
    return 0;
}
