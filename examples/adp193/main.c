/* The stock software's adaptation values on a real 193, read over CAN;
 * built as examples/adpclear193, channel 67 put back to 0 as well.
 *
 * The stock 193 software keeps its diagnostic adaptation channels in the
 * battery-backed memory: a block of 0xac bytes at 0x18003de4,
 *
 *   +0x00  mark 0x12345678
 *   +0x04  version (0x02220004)
 *   +0x08  the sum of the 39 values below
 *   +0x0c  39 values of 16 bits, in the order of the software's channel
 *          table (tools/adp193.py has it); channel 67 is the sixth
 *   +0xa8  mark 0xedcba987
 *
 * and a copy as block 3 of its store in the flash memory (two banks of
 * 128 KiB at 0x033e0000 and 0x03400000, the one with the higher sequence
 * number in use), which it takes only when the block in the
 * battery-backed memory is not valid.  Channel 67 at 255 makes the stock
 * software refuse CD EJECT (rnse's docs/hardware-193.md, "Adaptation").
 *
 * Started by the SD loader (examples/sdload193) from a card or over the
 * bus, as examples/dump193: no board bring-up, no port, no key.  It
 * reads through P2, reports, and starts the unit again.
 *
 * Records on 0x7F8 (test, item, value[4], second[2]):
 *   0xa0 0     value = the block's mark, second = the version's low half
 *   0xa1 0     value = the stored sum, second = 1 if the end mark is right
 *   0xa1 1     value = the sum of the values as read
 *   0xa2 i     value = value i (0..38)
 *   0xa3 b     flash store bank b (0, 1): value = sequence number,
 *              second = bit 0 its mark is there, bit 1 its checksum is
 *              right, bit 2 it has a block 3
 *   0xa4 b     that block 3: value = its stored sum, second = its
 *              channel 67
 *   0xa5 s     the end: s = 0 nothing written (this program), 1 channel
 *              67 cleared and read back, 2 not written (the block is not
 *              as expected: see below), 3 written but read back
 *              otherwise; value = channel 67 now << 16 | 1 if the sum
 *              matches now, second = the stored sum's low half now
 *   0xff 0xff  before the restart
 *
 * ADP_CLEAR (examples/adpclear193) writes three words of the
 * battery-backed memory and nothing else, and only if both marks are
 * right, the stored sum is the sum of the values, and channel 67 is
 * 0x00ff: the value becomes 0 and the sum 0xff less.  The flash store is
 * not touched: its copy keeps the old value, and would come back should
 * the battery-backed memory ever be lost.
 */
#include "rnse.h"

#define CAN 0xffdf0000
#define WATCHDOG 0xffc44008

#define BLOCK 0xb8003de4u /* P2 of 0x18003de4 */
#define VALUES 39
#define CH67 5
#define STORE0 0xa33e0000u /* P2 of the flash store's banks */
#define STORE1 0xa3400000u

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

static u32 total(void)
{
    u32 sum = 0;
    int i;
    for (i = 0; i < VALUES; i++)
        sum += (u32)(s32)(s16)REG16(BLOCK + 0x0c + 2 * i); /* as the stock software adds them */
    return sum;
}

static void bank(u8 n, u32 base)
{
    static const u8 mark[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    u32 flags = 1, sum = 0, off, i, found = 0;
    for (i = 0; i < 8; i++)
        if (REG8(base + 0x5fe8 + i) != mark[i])
            flags = 0;
    for (off = 0; off < 0x1fff8; off += 4) {
        sum += REG32(base + off);
        if ((off & 0xfff) == 0)
            feed();
    }
    if (sum == REG32(base + 0x1fff8))
        flags |= 2;
    if (flags & 1)
        for (i = 0; i < 93 && !found; i++) {
            u32 e = base + 0x5d00 + i * 8;
            if (REG16(e) == 3 && REG16(e + 2) >= 0xac && REG16(e + 4) < 93)
                found = base + REG16(e + 4) * 256u;
        }
    if (found)
        flags |= 4;
    say(0xa3, n, REG32(base + 0x1fffc), flags);
    if (found)
        say(0xa4, n, REG32(found + 8), REG16(found + 0x0c + 2 * CH67));
}

int main(void)
{
    u32 sum, status = 0;
    int i;

    /* Take back whatever the stock software had queued on mailbox 1, as
     * sdload193 does, then report. */
    REG32(CAN + 0x28) = 0xfffffffe;

    sum = total();
    say(0xa0, 0, REG32(BLOCK), REG32(BLOCK + 4));
    say(0xa1, 0, REG32(BLOCK + 8), REG32(BLOCK + 0xa8) == 0xedcba987u);
    say(0xa1, 1, sum, 0);
    for (i = 0; i < VALUES; i++)
        say(0xa2, (u8)i, REG16(BLOCK + 0x0c + 2 * i), 0);
    bank(0, STORE0);
    bank(1, STORE1);

#ifdef ADP_CLEAR
    if (REG32(BLOCK) == 0x12345678u && REG32(BLOCK + 0xa8) == 0xedcba987u
        && REG32(BLOCK + 8) == sum && REG16(BLOCK + 0x0c + 2 * CH67) == 0x00ff) {
        REG16(BLOCK + 0x0c + 2 * CH67) = 0;
        REG32(BLOCK + 8) = sum - 0xff;
        status = REG16(BLOCK + 0x0c + 2 * CH67) == 0 && REG32(BLOCK + 8) == sum - 0xff
                 && total() == sum - 0xff ? 1 : 3;
    } else {
        status = 2;
    }
#endif
    say(0xa5, (u8)status, (u32)REG16(BLOCK + 0x0c + 2 * CH67) << 16 | (total() == REG32(BLOCK + 8)),
        REG32(BLOCK + 8));
    say(0xff, 0xff, 0, 0);
    restart();
    return 0;
}
