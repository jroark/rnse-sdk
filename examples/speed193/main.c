/* How fast the 193 reads the SD bridge, and whether every word comes
 * right: LINKTEST.BIN on the bridge's card is 8 MB in which word j (big
 * endian) is pattern(j) (rnse-sdbridge src/volume.c), so each sector is
 * checked as it arrives.  One sector a command (CMD17), then CMD18 with
 * 8, 64 and 256 blocks a command, at the kit's clock divider (1) and at
 * the stock software's (0).  Nothing is written, no key is read, the
 * drive is not touched; then the unit starts again.
 *
 * Records on 0x7F8 (test, item, value, second):
 *   0xb0 0     alive; 0xb0 1 board_init done; 0xb0 2 sd_init (value);
 *              0xb0 3 sd_open (value), second = the file's first sector
 *   0xb1 n     test n: value = KB/s, second = sectors read
 *   0xb2 n     test n: value = bad sectors, second = the first bad one
 *              (0xffff: none); 0xb3 n the first read's result, when one
 *              failed (value), and the sector it was for (second)
 *   0xff 0xff  the end */
#include "rnse.h"

#define CAN 0xffdf0000
#define CAN_MB1 (CAN + 0x100 + 0x20)
#define BUFFER ((u8 *)0x8c000000)

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
    for (n = 0; n < 400000; n++)
        if (REG16(CAN + 0x32) & 0x0002)
            break;
    if (REG16(CAN + 0x32) & 0x0002) {
        REG16(CAN + 0x32) = 0x0002;
        return;
    }
    REG32(CAN + 0x28) = 0x00000002;
    for (n = 0; n < 400000; n++)
        if (!(REG16(CAN + 0x22) & 0x0002) || (REG16(CAN + 0x3a) & 0x0002))
            break;
    REG16(CAN + 0x3a) = 0x0002;
}

static u32 pattern(u32 j)
{
    u32 x = (j + 1) * 0x9e3779b1u;
    x ^= x >> 16;
    x *= 0x85ebca6bu;
    x ^= x >> 13;
    return x;
}

/* Sector s of the file in p: every word as it should be? */
static int sector_ok(const u8 *p, u32 s)
{
    u32 i;
    for (i = 0; i < 128; i++) {
        u32 w = (u32)p[4 * i] << 24 | (u32)p[4 * i + 1] << 16 | (u32)p[4 * i + 2] << 8 | p[4 * i + 3];
        if (w != pattern(s * 128 + i))
            return 0;
    }
    return 1;
}

static u32 first_sector;

/* Test n: total sectors in commands of `per` blocks (per 1: CMD17). */
static void run(u8 n, u32 total, u32 per)
{
    u32 t, done = 0, bad = 0, first_bad = 0xffff, s;
    int r, reported = 0;
    t = micros();
    while (done < total) {
        u32 count = per < total - done ? per : total - done;
        u32 room = 0x20000 / 512;
        if (count > room)
            count = room;
        if (per == 1)
            r = sdhi_read(first_sector + done, BUFFER);
        else
            r = sdhi_read_multi(first_sector + done, count, BUFFER);
        if (r && !reported) {
            say(0xb3, n, (u32)r, done);
            reported = 1;
        }
        for (s = 0; s < count; s++)
            if (r || !sector_ok(BUFFER + 512 * s, done + s)) {
                if (!bad)
                    first_bad = done + s;
                bad++;
            }
        done += count;
    }
    t = micros() - t;
    say(0xb1, n, t ? udiv(total * 512 * 1000u, t) * 1000u / 1024u : 0, total);
    say(0xb2, n, bad, first_bad);
}

int main(void)
{
    static u32 chain[4096];
    struct sd_file f;
    int r;

    REG32(CAN + 0x28) = 0xfffffffe;
    say(0xb0, 0, 0, 0);
    board_init();
    say(0xb0, 1, millis(), 0);
    screen_palette(1, 255, 255, 255);
    screen_text(16, 16, "SPEED 193: READING THE BRIDGE", 1, 2);

    sdhi_fast_clock = 1;
    r = sd_init(0);
    say(0xb0, 2, (u32)r, 0);
    if (r == 0) {
        r = sd_open("LINKTESTBIN", &f, chain, 4096);
        first_sector = f.data_start + (chain[0] - 2) * f.per_cluster;
        say(0xb0, 3, (u32)r, first_sector);
        if (r == 0) {
            run(1, 256, 1);
            run(2, 1024, 8);
            run(3, 2048, 64);
            run(4, 2048, 256);
            sdhi_fast_clock = 0;
            if (sd_init(0) == 0) {
                run(5, 2048, 64);
                run(6, 4096, 256);
            }
        }
    }
    say(0xff, 0xff, millis(), 0);
    delay_ms(3000);
    restart();
    return 0;
}
