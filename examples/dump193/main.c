/* A least-disturbance memory dump for a real 193, over CAN.
 *
 * Started by the SD loader (examples/sdload193) as RNSE.BIN from a card:
 * by then the loader has already brought the board up (board_init) and
 * left the CAN controller as the stock software had it (mailbox 1, no
 * reset).  This program does nothing more to the unit: it does not call
 * board_init again, touches no port, reads no key and writes nothing.
 * It takes a snapshot of a window of memory the moment it starts, reads
 * it through P2 (uncached, so neither the cache's state nor its stale
 * lines matter), sends the snapshot out on the CAN bus, and starts the
 * unit again.  The one thing it must keep doing is strobing the external
 * watchdog, as every program here does, so the dump is not cut short.
 *
 * The point is to catch the stock software's own DRAM as it stood: a
 * loader-started program lives at 0x09b60000 (1 MiB) with its stack just
 * below the end of that place, far from the window below, and board_init
 * does not clear DRAM, so a window up in DRAM keeps what the stock
 * software last wrote there.
 *
 * The window is set at build time (default: the 4 KiB page holding the
 * physical address 0x0e9de900).  EXTRA_CFLAGS=-DDUMP_BASE=0x..  -DDUMP_SIZE=0x..
 *
 * Records on 0x7F8 (test, item, value[4], second[2]), as sdload193:
 *   0xd0 0   alive: value = DUMP_BASE, second = SR's low half
 *   0xd1 0   the window: value = DUMP_BASE, second = DUMP_SIZE >> 4
 *   0xd2 0   done: value = CRC-32 of the whole window
 *   0xff 0xff the end, before restart
 * and the bytes on 0x7F9: three bytes of offset from DUMP_BASE, then
 * five bytes of data (the last frame padded with zeroes; the length is
 * known from DUMP_SIZE).  tools/dump193.py reassembles it.
 */
#include "rnse.h"

#ifndef DUMP_BASE
#define DUMP_BASE 0x0e9de000u
#endif
#ifndef DUMP_SIZE
#define DUMP_SIZE 0x1000u
#endif

#define CAN 0xffdf0000
#define CAN_MB1 (CAN + 0x100 + 0x20)
#define WATCHDOG 0xffc44008

/* P2, the uncached window onto physical memory. */
#define P2(a) (0xa0000000u | (a))

static u32 strobe;

static void feed(void)
{
    strobe ^= 1;
    REG32(WATCHDOG) = (REG32(WATCHDOG) & ~1u) | strobe;
}

/* One frame on mailbox 1, with a counted wait and the watchdog fed, as
 * examples/sdload193's say(). */
static void frame(u16 id, const u8 d[8])
{
    can_tell(id, d, 8, feed);
    feed();
}

static void say(u8 test, u8 item, u32 value, u32 second)
{
    u8 d[8] = { test, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8),
                (u8)value, (u8)(second >> 8), (u8)second };
    frame(0x7f8, d);
}

static u32 crc32(const u8 *p, u32 n)
{
    u32 c = 0xffffffff;
    while (n--) {
        int k;
        c ^= *p++;
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}

int main(void)
{
    static u8 snap[DUMP_SIZE];
    u32 sr, off;
    u32 i;

    __asm__ volatile("stc sr, %0" : "=r"(sr));

    /* Snapshot the window first thing, uncached, before anything else
     * this program does can touch memory beyond its own place. */
    for (i = 0; i < DUMP_SIZE; i++)
        snap[i] = REG8(P2(DUMP_BASE + i));

    /* Take back whatever the stock software had queued on mailbox 1, as
     * sdload193 does, then report. */
    REG32(CAN + 0x28) = 0xfffffffe;
    say(0xd0, 0, DUMP_BASE, sr);
    say(0xd1, 0, DUMP_BASE, DUMP_SIZE >> 4);

    for (off = 0; off < DUMP_SIZE; off += 5) {
        u8 d[8];
        d[0] = (u8)(off >> 16);
        d[1] = (u8)(off >> 8);
        d[2] = (u8)off;
        for (i = 0; i < 5; i++)
            d[3 + i] = off + i < DUMP_SIZE ? snap[off + i] : 0;
        frame(0x7f9, d);
    }

    say(0xd2, 0, crc32(snap, DUMP_SIZE), 0);
    say(0xff, 0xff, 0, 0);
    restart();
    return 0;
}
