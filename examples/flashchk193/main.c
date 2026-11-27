/* What is in a 193's flash where its software should be, over CAN: for
 * a unit whose boot ROM no longer starts it ("Set Original CD-ROM").
 *
 * Started by the SD loader (examples/sdload193) from a card or over the
 * bus, as examples/dump193: no board bring-up, no port, no key, and no
 * write of any kind.  It reads through P2 and reports:
 *
 * Records on 0x7F8 (test, item, value[4], second[2]):
 *   0xb0 0     the start: value = the first word of the module
 *              directory (0x00400000)
 *   0xb1 i     module i (0 the OS at 0x00400200, 1.. the directory's
 *              further entries): value = the address of its header,
 *              second = bit 0 the header begins with '.', bit 1 "\r\n"
 *              follows the tag, bit 2 its own address field agrees,
 *              bit 3 the size is sane and the sum was computed
 *   0xb2 i     value = characters 0-3 of its version
 *   0xb3 i     value = characters 4-7
 *   0xb4 i     value = the size in its header
 *   0xb5 i     value = the checksum in its header
 *   0xb6 i     value = the sum of the image as it is in flash
 *   0xb7 n     the battery-backed memory's first 32 bytes, four at a
 *              time (n = 0..7)
 *   0xb8 0     the end of the report: value = the number of modules
 *   0xff 0xff  before the restart
 * and on 0x7F9, three bytes of offset and five of data:
 *   offsets 0x000000-0x0001ff  the module directory, 512 bytes
 *   offsets 0x010000-0x01007f  one bit for each 64 KiB of the flash
 *              (0x00000000-0x03ffffff, 1024 sectors, the first in bit 7
 *              of the first byte): 1 = nothing but 0xff there
 *   offsets 0x020000 + 4n  (built with EXTRA_CFLAGS=-DPAGES, after record
 *              0xb9 i: value = the module's address, second = its 4 KiB
 *              pages) the sum of the words of page n of the first module
 *              whose checksum is wrong, to find what changed in it
 * tools/flashchk193.py prints it.
 */
#include "rnse.h"

#define CAN 0xffdf0000
#define WATCHDOG 0xffc44008
#define P2(a) (0xa0000000u | ((a) & 0x1fffffffu))
#define DIRECTORY 0x00400000u
#define FLASH_END 0x04000000u

static u32 strobe;

static void feed(void)
{
    strobe ^= 1;
    REG32(WATCHDOG) = (REG32(WATCHDOG) & ~1u) | strobe;
}

static void say(u8 test, u8 item, u32 value, u32 second)
{
    u8 d[8] = { test, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8),
                (u8)value, (u8)(second >> 8), (u8)second };
    can_tell(0x7f8, d, 8, feed);
    feed();
}

static void bytes(u32 off, const u8 *p, u32 n)
{
    u32 i, k;
    for (i = 0; i < n; i += 5) {
        u8 d[8];
        d[0] = (u8)((off + i) >> 16);
        d[1] = (u8)((off + i) >> 8);
        d[2] = (u8)(off + i);
        for (k = 0; k < 5; k++)
            d[3 + k] = i + k < n ? p[i + k] : 0;
        can_tell(0x7f9, d, 8, feed);
        feed();
    }
}

/* Four bytes at any address (REG32 wants a multiple of four). */
static u32 four(u32 a)
{
    return (u32)REG8(a) << 24 | (u32)REG8(a + 1) << 16 | (u32)REG8(a + 2) << 8 | REG8(a + 3);
}

static int paged;

static void module(u8 i, u32 at)
{
    u32 flags = 0, size, sum = 0, off;
    at &= 0x1fffffffu;
    if (at >= FLASH_END - 0x90 || (at & 3)) {
        say(0xb1, i, at, 0x80);
        return;
    }
    size = REG32(P2(at + 0x74));
    if (REG8(P2(at)) == '.')
        flags |= 1;
    if (REG8(P2(at + 4)) == '\r' && REG8(P2(at + 5)) == '\n')
        flags |= 2;
    if ((REG32(P2(at + 0x70)) & 0x1fffffffu) == at)
        flags |= 4;
    if (size >= 0x80 && size <= FLASH_END - at) {
        flags |= 8;
        for (off = 0x80; off + 4 <= size; off += 4) {
            sum += REG32(P2(at + off));
            if ((off & 0xfffc) == 0)
                feed();
        }
        if (off < size) {
            u32 last = 0, k;
            for (k = 0; off + k < size; k++)
                last |= (u32)REG8(P2(at + off + k)) << (24 - 8 * k);
            sum += last;
        }
    }
    /* A module whose sum is wrong: the sum of each 4 KiB of it, for
     * comparing with the disc's module (PAGES=1 builds only). */
#ifdef PAGES
    if ((flags & 8) && sum != REG32(P2(at + 0x78)) && !paged) {
        u32 page;
        paged = 1;
        say(0xb9, i, at, (size + 0xfff) >> 12);
        for (page = 0; page << 12 < size; page++) {
            u32 s = 0, o;
            u8 d[8];
            for (o = 0; o < 0x1000; o += 4)
                s += REG32(P2(at + (page << 12) + o));
            d[0] = 2;
            d[1] = (u8)(page >> 6);
            d[2] = (u8)(page << 2);
            d[3] = (u8)(s >> 24);
            d[4] = (u8)(s >> 16);
            d[5] = (u8)(s >> 8);
            d[6] = (u8)s;
            d[7] = 0;
            can_tell(0x7f9, d, 8, feed);
            feed();
        }
    }
#endif
    say(0xb1, i, at, flags);
    say(0xb2, i, four(P2(at + 6)), 0);
    say(0xb3, i, four(P2(at + 10)), 0);
    say(0xb4, i, size, 0);
    say(0xb5, i, REG32(P2(at + 0x78)), 0);
    say(0xb6, i, sum, 0);
}

int main(void)
{
    static u8 dir[512], map[128];
    u32 i, n, sector;
    u8 count = 1;

    REG32(CAN + 0x28) = 0xfffffffe;
    for (i = 0; i < sizeof dir; i++)
        dir[i] = REG8(P2(DIRECTORY + i));
    say(0xb0, 0, REG32(P2(DIRECTORY)), 0);
    bytes(0, dir, sizeof dir);

    module(0, DIRECTORY + 0x200);
    n = (u32)dir[0x18] << 24 | (u32)dir[0x19] << 16 | (u32)dir[0x1a] << 8 | dir[0x1b];
    if (n <= 8)
        for (i = 0; i < n; i++) {
            const u8 *e = dir + 0x1c + 16 * i;
            if (e[0] == '.')
                module(count++, (u32)e[12] << 24 | (u32)e[13] << 16 | (u32)e[14] << 8 | e[15]);
        }

    for (i = 0; i < 8; i++)
        say(0xb7, (u8)i, REG32(0xb8000000u + 4 * i), 0);

    for (sector = 0; sector < 1024; sector++) {
        u32 base = sector << 16, off, all = 0xffffffffu;
        for (off = 0; off < 0x10000 && all == 0xffffffffu; off += 4)
            all &= REG32(P2(base + off));
        if (all == 0xffffffffu)
            map[sector >> 3] |= (u8)(0x80 >> (sector & 7));
        feed();
    }
    bytes(0x10000, map, sizeof map);

    say(0xb8, 0, count, 0);
    say(0xff, 0xff, 0, 0);
    restart();
    return 0;
}
