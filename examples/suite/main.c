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
 *       (1, 2).  RETURN ends it early, after three seconds.
 *       With them, in the same thirty seconds: every change of a
 *       port's pins (3: port, data), of the processor's port A (4) and,
 *       by more than 16, of an A/D input (5: input, reading), for
 *       whatever is done to the unit meanwhile (a card put in, the
 *       display moved by hand, a light on the sensor)
 *   11  the six serial channels: mode, bit rate divisor, control and
 *       status as the installed software left them (items 0..5), and
 *       what arrives on each in a second (16 + 16 x channel onwards,
 *       four bytes a record, the count in the second value)
 *   12  the radio processor asked two things its own software asks:
 *       its version (0x0B; items from 0) and its status (0x88; items
 *       from 64), the answers as they came, four bytes a record
 *   13  the two flash chips say what they are: manufacturer and device
 *       (items 0, 1 and 64, 65) and, if they answer the common flash
 *       interface query, its 48 words from 0x10 (items 2.. and 66..),
 *       where a chip states how long an erase and a write take.  Read
 *       only: no erase or write command is given
 *   14  the drive in full: INQUIRY (96 bytes, items from 0), its
 *       capabilities page (MODE SENSE 0x2A, from 32) and the disc's
 *       table of contents (from 64)
 *   15  the bus: microseconds for 8192 words of the flash through the
 *       cache (0) and past it (1), and of the battery-backed memory (2)
 *   16  the GPS baseband's registers, read (192)
 *   17  a disc change, if CD EJECT is pressed within fifteen seconds
 *       (SETUP leaves it out at once):
 *       the disc comes out and is waited for for a minute; then the
 *       milliseconds until the SDK's cd_wait_ready has the drive
 *       ready (item 0, with its result) and 64 sectors read (1)
 *   18  the battery-backed memory, all that the stock software uses of
 *       it, on frames 0x7F9: three bytes of address, five of data.
 *       Three minutes; SETUP skips the rest
 *   10  the watchdog: its strobe is left alone and a record follows
 *       every 10 ms with the milliseconds since, until the unit is
 *       reset.  Without a reset in ten seconds the unit is restarted.
 *
 * Item 0xfe opens a test (value = the clock in ms); test 0xff closes
 * the run.  The unit starts its software again at the
 * end and, with the disc still in the drive, this program after it.
 *
 * EXTRA_CFLAGS: -DHANDS=n seconds for test 9 (default 30). */
#include "rnse.h"

#ifndef HANDS
#define HANDS 45
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
    if (++row >= 22)
        row = 1;
}

/* A phase that wants hands says so where it cannot be missed: across
 * the bottom of the screen, with the seconds that are left. */
static void prompt(const char *text, u32 seconds)
{
    static u32 shown = 0xffffffff;
    static const char *what;
    char line[48], *p = line;
    int y = screen_height - 28 * s;
    if (text == what && seconds == shown)
        return;
    what = text;
    shown = seconds;
    screen_fill(0, y - 2 * s, screen_width, 30 * s, text ? 2 : 0);
    if (!text)
        return;
    screen_text(4 * s, y, text, 3, 2 * s);
    p = put_dec(p, seconds);
    p = put_str(p, " S LEFT");
    *p = 0;
    screen_text(4 * s, y + 16 * s, line, 3, s);
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
    u32 start = millis(), keys = 0xffffffff, more = 0xffffffff, looked = 0;
    int volume = 0, select = 0, k, turned;
    u8 pins[8];
    u16 own = 0, inputs[7];
    for (k = 0; k < 8; k++)
        pins[k] = 0;
    for (k = 0; k < 7; k++)
        inputs[k] = 0xffff;
    open(9, "KEYS AND KNOBS: PRESS, TURN.  RETURN ENDS");
    while (millis() - start < HANDS * 1000u) {
        u32 now = 0, now2 = 0;
        prompt("KEYS, KNOBS, CARDS: NOW", HANDS - udiv(millis() - start, 1000));
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
        if (panel_key(KEY_RETURN) && millis() - start > 3000)
            break;
        if (screen_width == 400 && millis() - looked >= 100) {
            looked = millis();
            for (k = 0; k < 8; k++) {
                /* (Not the strobe, nor the lines of the card
                 * processors' handshake, which move by themselves.) */
                u8 mask = k == 0 ? 0xfe : k == 1 ? 0xf0 : 0xff;
                u8 now8 = REG8(0xb2300001 + 0x100000 * (u32)k) & mask;
                if (now8 != pins[k]) {
                    pins[k] = now8;
                    rec(9, 3, (u32)k, now8);
                }
            }
            if (REG16(0xff800030) != own) {
                own = REG16(0xff800030);
                rec(9, 4, own, 0);
            }
            for (k = 0; k < 7; k++) {
                u32 t0 = millis();
                u16 v = 0xffff;
                REG16(ADC + 8) = (u16)(0x2000 | k);
                while (millis() - t0 < 20) {
                    if (REG16(ADC + 8) & 0x8000) {
                        v = REG16(ADC + (k < 4 ? 2 * k : 2 * (k - 4))) >> 6;
                        REG16(ADC + 8) = 0;
                        break;
                    }
                }
                if (v != 0xffff && (inputs[k] == 0xffff || v > inputs[k] + 16 || v + 16 < inputs[k])) {
                    inputs[k] = v;
                    rec(9, 5, (u32)k, v);
                }
            }
        }
        cpu_idle();
    }
    prompt(0, 0);
}

/* Bytes as records: four in the value, the count so far in the second. */
static void rec_bytes(u8 test, u8 first, const u8 *b, u32 n)
{
    u32 i;
    for (i = 0; i < n; i += 4) {
        u32 v = 0, k;
        for (k = 0; k < 4; k++)
            v = v << 8 | (i + k < n ? b[i + k] : 0);
        rec(test, (u8)(first + i / 4), v, i + 4 < n ? i + 4 : n);
    }
}

#define UART(n) (0xa7a00000 + 0x10000 * (u32)(n))

/* What a serial channel has received, taken out as it comes, until
 * nothing has come for `quiet` ms or `ms` are over. */
static u32 listen(int ch, u8 *out, u32 max, u32 ms, u32 quiet)
{
    u32 start = millis(), last = start, n = 0;
    /* A receiver that has overrun (its 32 bytes were full: the
     * installed software's interrupt is not there to empty it) hears
     * nothing more until its error flags are taken back. */
    REG16(UART(ch) + 0x08) = REG16(UART(ch) + 0x08) & 0xffc7;
    while (millis() - start < ms && n < max) {
        if (REG16(UART(ch) + 0x08) & 0x0040) {
            out[n++] = (u8)REG16(UART(ch) + 0x0a);
            last = millis();
        } else if (n && millis() - last > quiet) {
            break;
        }
    }
    return n;
}

static void serial_channels(void)
{
    int ch;
    open(11, "SERIAL CHANNELS");
    for (ch = 0; ch < 6; ch++) {
        u32 n;
        rec(11, (u8)ch, (u32)REG16(UART(ch)) << 16 | REG16(UART(ch) + 2), REG16(UART(ch) + 4));
        rec(11, (u8)(8 + ch), REG16(UART(ch) + 8), REG16(UART(ch) + 0x0c));
        if (ch == 3 || ch == 5)
            continue; /* the faceplate's, which has its driver, and the debug port */
        n = listen(ch, buffer, 48, 1000, 1000);
        rec_bytes(11, (u8)(16 + 16 * ch), buffer, n);
        result("BYTES ON A CHANNEL", n, 0);
    }
}

static void radio_ask(u8 cmd, u8 first)
{
    u8 frame[5] = { 0x01, cmd, 0, 0, 0 };
    u32 i, n;
    u16 control = REG16(UART(1) + 0x04);
    frame[4] = (u8)~(frame[0] + frame[1]);
    /* What is waiting there already is not the answer. */
    listen(1, buffer, 256, 50, 20);
    /* The installed software has the transmitter on only while it
     * sends (control 0x58 between frames): on for these five bytes,
     * and as it was afterwards. */
    REG16(UART(1) + 0x04) = (u16)((control | 0x0030) & ~0x00c4);
    for (i = 0; i < sizeof frame; i++) {
        u32 t0 = millis();
        while ((REG16(UART(1) + 0x08) & 0x0200) && millis() - t0 < 100)
            ;
        REG16(UART(1) + 0x06) = frame[i];
    }
    n = listen(1, buffer, 120, 1500, 200);
    REG16(UART(1) + 0x04) = control;
    rec(12, (u8)(first + 62), n, cmd);
    rec_bytes(12, first, buffer, n);
    result("BYTES ANSWERED", n, 0);
}

static void radio_questions(void)
{
    open(12, "RADIO PROCESSOR, ASKED");
    radio_ask(0x0b, 0);
    radio_ask(0x88, 64);
}

void irq_disable(void);
void irq_enable(void);

static void flash_chips(void)
{
    int chip;
    open(13, "FLASH CHIPS");
    for (chip = 0; chip < 2; chip++) {
        u32 base = 0xa0000000 + 0x01000000 * (u32)chip, i, id[2], cfi[48];
        /* Nothing may read the flash while it answers questions, and
         * nothing does: this program is in RAM.  Interrupts off all the
         * same, for the fraction of a millisecond it takes. */
        irq_disable();
        REG32(base + 0x555 * 4) = 0xaa;
        REG32(base + 0x2aa * 4) = 0x55;
        REG32(base + 0x555 * 4) = 0x90;
        id[0] = REG32(base);
        id[1] = REG32(base + 4);
        REG32(base) = 0xf0;
        REG32(base + 0x55 * 4) = 0x98;
        for (i = 0; i < 48; i++)
            cfi[i] = REG32(base + (0x10 + i) * 4);
        REG32(base) = 0xf0;
        irq_enable();
        rec(13, (u8)(64 * chip), id[0], 0);
        rec(13, (u8)(64 * chip + 1), id[1], 0);
        for (i = 0; i < 48; i++)
            rec(13, (u8)(64 * chip + 2 + i), cfi[i], 0);
        result("MANUFACTURER AND DEVICE", id[0] << 16 | (id[1] & 0xffff), 1);
        result("QUERY", (cfi[0] & 0xff) << 16 | (cfi[1] & 0xff) << 8 | (cfi[2] & 0xff), 1);
    }
}

static void drive_details(void)
{
    static const u8 inquiry[12] = { 0x12, 0, 0, 0, 96 };
    static const u8 capabilities[12] = { 0x5a, 0, 0x2a, 0, 0, 0, 0, 0, 96, 0 };
    static const u8 contents[12] = { 0x43, 0, 0, 0, 0, 0, 0, 0, 64, 0 };
    u32 i;
    open(14, "DRIVE IN FULL");
    for (i = 0; i < 128; i++)
        buffer[i] = 0;
    rec(14, 250, (u32)cd_command(inquiry, buffer, 96), 0);
    rec_bytes(14, 0, buffer, 96);
    for (i = 0; i < 128; i++)
        buffer[i] = 0;
    rec(14, 251, (u32)cd_command(capabilities, buffer, 96), 0);
    rec_bytes(14, 32, buffer, 96);
    for (i = 0; i < 128; i++)
        buffer[i] = 0;
    rec(14, 252, (u32)cd_command(contents, buffer, 64), 0);
    rec_bytes(14, 64, buffer, 64);
}

static void bus(void)
{
    u32 t, sum;
    open(15, "BUS");
    t = micros();
    sum = add_up((const u32 *)0x80100000, 8192);
    t = micros() - t;
    rec(15, 0, t, sum);
    result("FLASH, CACHED US", t, 0);
    t = micros();
    sum = add_up((const u32 *)0xa0108000, 8192);
    t = micros() - t;
    rec(15, 1, t, sum);
    result("FLASH, UNCACHED US", t, 0);
    t = micros();
    sum = add_up((const u32 *)0xb0000000, 8192);
    t = micros() - t;
    rec(15, 2, t, sum);
    result("BATTERY-BACKED MEMORY US", t, 0);
}

static void gps(void)
{
    int i;
    open(16, "GPS BASEBAND");
    for (i = 0; i < 6; i++)
        rec(16, (u8)i, REG32(0xb7c00000 + 4 * (u32)i), 0);
    for (i = 0; i < 3; i++)
        rec(16, (u8)(8 + i), REG16(0xa7e00000 + 2 * (u32)i), 0);
    result("STATUS", REG32(0xb7c00014), 1);
}

static int drive_state(void)
{
    static const u8 test_unit_ready[12] = { 0x00 };
    static const u8 request_sense[12] = { 0x03, 0, 0, 0, 18 };
    u8 sense[18];
    if (cd_command(test_unit_ready, 0, 0) == 0)
        return 0;
    if (cd_command(request_sense, sense, sizeof sense))
        return -1;
    return (sense[2] & 15) << 16 | sense[12] << 8 | sense[13];
}

static void disc_change(void)
{
    static const u8 allow_removal[12] = { 0x1e };
    static const u8 eject[12] = { 0x1b, 1, 0, 0, 2 };
    u32 start = millis(), t;
    int r;
    open(17, "A DISC CHANGE: CD EJECT WITHIN 15 S, OR NOT");
    while (!panel_key(KEY_CD_EJECT)) {
        prompt("CD EJECT FOR A DISC CHANGE", 15 - udiv(millis() - start, 1000));
        panel_poll();
        if (millis() - start > 15000 || panel_key(KEY_SETUP)) {
            prompt(0, 0);
            rec(17, 255, 0, 0);
            show("   LEFT OUT");
            return;
        }
        cpu_idle();
    }
    cd_command(allow_removal, 0, 0);
    cd_command(eject, 0, 0);
    prompt("TAKE THE DISC, PUSH IT BACK", 80);
    show("   TAKE THE DISC, PUSH IT BACK IN");
    /* Out: no medium.  Then in again: anything else. */
    start = millis();
    while ((drive_state() >> 8) != 0x23a && millis() - start < 20000)
        delay_ms(100);
    start = millis();
    while ((drive_state() >> 8) == 0x23a) {
        if (millis() - start > 60000) {
            rec(17, 253, 0, 0);
            show("   NO DISC CAME BACK");
            return;
        }
        delay_ms(100);
    }
    prompt("WAITING FOR THE DRIVE", 40);
    t = millis();
    r = cd_wait_ready(40000);
    t = millis() - t;
    prompt(0, 0);
    rec(17, 0, t, (u32)r);
    result("READY AFTER MS", t, 0);
    t = r ? 0xffffffff : timed_read(16, 64);
    rec(17, 1, t, 0);
    result("64 SECTORS US", t, 0);
}

static void memory_dump(void)
{
    const u8 *m = (const u8 *)0xb0000000;
    u32 at, size = 0x3f640;
    open(18, "BATTERY-BACKED MEMORY, 3 MIN.  SETUP SKIPS");
    for (at = 0; at < size; at += 5) {
        u8 d[8] = { (u8)(at >> 16), (u8)(at >> 8), (u8)at, m[at], m[at + 1], m[at + 2], m[at + 3], m[at + 4] };
        can_send_for(0x7f9, d, 8, 3000);
        if ((at & 0xfff) < 5) {
            prompt("SENDING MEMORY: HANDS OFF", udiv((size - at) * 4, 1000));
            panel_poll();
            if (panel_key(KEY_SETUP))
                break;
        }
    }
    prompt(0, 0);
    rec(18, 0, at, size);
    result("BYTES SENT", at, 0);
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
    screen_palette(2, 200, 40, 0);
    screen_palette(3, 255, 255, 255);
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
    if (screen_width == 400) {
        serial_channels();
        radio_questions();
        flash_chips();
    }
    drive_details();
    if (screen_width == 400) {
        bus();
        gps();
    }
    hands();
    disc_change();
    if (screen_width == 400)
        memory_dump();
    watchdog();
    rec(0xff, 0xff, millis(), 0);
    show("RESTARTING");
    delay_ms(1000);
    restart();
    return 0;
}
