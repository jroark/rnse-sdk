/* Which of the 193's card slots does the SD host reach, and how?  On
 * the bench (2026-11-20) the kit's SD start with the host's slot select
 * (+0x02) at 1 reached the SD bridge in physical slot 2, as 0 did, and a
 * card in physical slot 1 never answered.  This tries each select value
 * in turn and tells them apart on both ends:
 *
 *   - CMD8's check pattern carries the try (0xa0 + 2 * round + select),
 *     and the bridge logs every CMD8 argument: its log shows which tries
 *     reached it;
 *   - whatever answers CMD2 sends its CID, which tells the bridge (CID
 *     product "SDBRG"/its own) from a real card.
 *
 * Nothing is written to any card; no key is read; the drive is not
 * touched.  Records on 0x7F8 (test, item, value, second), through the
 * CAN controller as the stock software left it:
 *
 *   0x90 0       alive; 0x90 1 board_init done
 *   0x91 t       try t = 2 * round + select: value = +0x1c (card status,
 *                upper half) | +0x1e, second = +0xf8; 0x92 t: +0x02 read
 *                back (upper half) | +0xfc
 *   0x93 t       CMD8: value = result (0 or minus the error bits),
 *                second = the response's low half
 *   0x94 t       ACMD41: value = rounds until ready (0xffff: never, or
 *                minus the error bits), second = OCR's upper half
 *   0x95 t       CMD2's CID, words 0..7 of the response registers, two a
 *                record: item = t * 16 + k
 *   0xff 0xff    the end; the unit starts again */
#include "rnse.h"

#define CAN 0xffdf0000
#define CAN_MB1 (CAN + 0x100 + 0x20)
#define SDHI 0xff90a000

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

static u16 clock_bits;

/* One command as src/hw193.c sends it: 0 or minus the error bits. */
static int command(u16 word, u32 arg)
{
    u32 start = millis();
    REG16(SDHI + 0x24) = clock_bits;
    REG16(SDHI + 0x24) = (u16)(clock_bits | 0x0100);
    REG16(SDHI + 0x04) = (u16)arg;
    REG16(SDHI + 0x06) = (u16)(arg >> 16);
    REG16(SDHI + 0x1c) = (u16)~0x0005;
    REG16(SDHI + 0x1e) = (u16)~0x807f;
    REG16(SDHI + 0x00) = word;
    for (;;) {
        u16 e = REG16(SDHI + 0x1e) & 0x807f;
        if (e)
            return -(int)e;
        if (REG16(SDHI + 0x1c) & 1)
            break;
        if (millis() - start > 500)
            return -0x10000;
    }
    REG16(SDHI + 0x1c) = (u16)~0x0001;
    return 0;
}

static u32 response(void)
{
    return (u32)REG16(SDHI + 0x0c) | (u32)REG16(SDHI + 0x0e) << 16;
}

static void try_slot(int round, int select)
{
    u8 t = (u8)(2 * round + select);
    u32 start;
    int r, rounds;

    REG16(SDHI + 0xe0) = 0;
    REG16(SDHI + 0xe0) = 7;
    REG16(SDHI + 0xd8) = 0;
    REG16(SDHI + 0x28) = 0x80d9;   /* one data line */
    REG16(SDHI + 0xf0) = 0x00da;
    REG16(SDHI + 0xfc) = 0x00fc;
    REG16(SDHI + 0x20) = 0xffff;
    REG16(SDHI + 0x22) = 0xffff;
    REG16(SDHI + 0xb2) = 0xffff;
    REG16(SDHI + 0x02) = (u16)select;
    clock_bits = 0x0020;
    REG16(SDHI + 0x24) = 0;
    delay_ms(20);
    say(0x91, t, (u32)REG16(SDHI + 0x1c) << 16 | REG16(SDHI + 0x1e), REG16(SDHI + 0xf8));
    say(0x92, t, (u32)REG16(SDHI + 0x02) << 16 | REG16(SDHI + 0xfc), 0);

    command(0x0000, 0);
    r = command(0x0008, 0x100 | (0xa0 + t)); /* the try in the check pattern */
    say(0x93, t, (u32)r, response() & 0xffff);

    rounds = 0;
    start = millis();
    for (;;) {
        r = command(0x0037, 0);
        if (r == 0)
            r = command(0x0069, 0x00ff8000);
        if (r) {
            say(0x94, t, (u32)r, 0);
            return;
        }
        rounds++;
        if (response() & 0x80000000)
            break;
        if (millis() - start > 1500) {
            say(0x94, t, 0xffff, response() >> 16);
            return;
        }
        delay_ms(20);
    }
    say(0x94, t, (u32)rounds, response() >> 16);
    r = command(0x0002, 0);
    if (r) {
        say(0x95, (u8)(t * 16 + 15), (u32)r, 0);
        return;
    }
    {
        int k;
        for (k = 0; k < 8; k += 2)
            say(0x95, (u8)(t * 16 + k), (u32)REG16(SDHI + 0x0c + 2 * k) << 16 | REG16(SDHI + 0x0e + 2 * k), 0);
    }
}

int main(void)
{
    int round, select;
    REG32(CAN + 0x28) = 0xfffffffe;
    say(0x90, 0, 0, 0);
    board_init();
    say(0x90, 1, millis(), 0);
    screen_palette(1, 255, 255, 255);
    screen_text(16, 16, "SLOT 193: WHICH SLOT ANSWERS", 1, 2);
    for (round = 0; round < 2; round++)
        for (select = 0; select < 2; select++)
            try_slot(round, select);
    say(0xff, 0xff, millis(), 0);
    delay_ms(3000);
    restart();
    return 0;
}
