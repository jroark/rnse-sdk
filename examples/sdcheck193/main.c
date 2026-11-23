/* The kit's SD driver on the 193's two slots: sd_init and sd_load in
 * slot 1 (select 1), slot 2 (select 0, the SD bridge) and slot 1 again,
 * switching both ways.  Each result, the file's size and its CRC-32 go
 * out on CAN; nothing is written, no key is read, the drive is not
 * touched; then the unit starts again.
 *
 *   0xa0 0      alive; 0xa0 1 board_init done
 *   0xa1 n      step n: sd_init(select) = value, second = select
 *   0xa2 n      sd_load: value = result, second = size >> 4 (size / 16)
 *   0xa3 n      CRC-32 of what was loaded
 *   0xff 0xff   the end */
#include "rnse.h"

#define CAN 0xffdf0000
#define CAN_MB1 (CAN + 0x100 + 0x20)
#define BUFFER ((u8 *)0x8c000000)

static void say(u8 test, u8 item, u32 value, u32 second)
{
    u8 d[8] = { test, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, (u8)(second >> 8), (u8)second };
    can_tell(0x7f8, d, 8, 0);
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

int main(void)
{
    static const int selects[3] = { 1, 0, 1 };
    static const char *const names[3] = { "RNSE    BIN", "LINKBENCBIN", "RNSE    BIN" };
    int n, r;
    u32 size;

    REG32(CAN + 0x28) = 0xfffffffe;
    say(0xa0, 0, 0, 0);
    board_init();
    say(0xa0, 1, millis(), 0);
    screen_palette(1, 255, 255, 255);
    screen_text(16, 16, "SD CHECK 193: SLOT 1, SLOT 2, SLOT 1", 1, 2);
    for (n = 0; n < 3; n++) {
        r = sd_init(selects[n]);
        say(0xa1, (u8)n, (u32)r, (u32)selects[n]);
        if (r)
            continue;
        size = 0;
        r = sd_load(names[n], BUFFER, 0x100000, &size);
        say(0xa2, (u8)n, (u32)r, size >> 4);
        if (r == 0)
            say(0xa3, (u8)n, crc32(BUFFER, size), 0);
    }
    say(0xff, 0xff, millis(), 0);
    delay_ms(3000);
    restart();
    return 0;
}
