/* Before a program goes into a unit's flash: what can be asked from a
 * card about the things it will rely on there.  The 192.
 *
 *   1  The guard's memory.  A program in flash counts its starts in
 *      the battery-backed memory at 0x3ff00 (sdk/src/flash.c).  This
 *      writes a count of its own next to it (0x3ff10) and reads it
 *      back, at every run one more: "RUN n" has to go up by one each
 *      time, through a restart and through the power being off.  It
 *      also says what is in the guard's own three words, without
 *      touching them.
 *   2  The flash chips' identity, by the commands the erase starts
 *      with: that they answer here is that the path to them works
 *      from a program's place.  Nothing is erased or written.
 *
 * On the CAN bus (0x7F8, test 0x32): item 0 the run's number and what
 * was read back; 1..3 the guard's words; 4 and 5 the two chips'
 * manufacturer and device.  It restarts the unit after 15 seconds. */
#include "rnse.h"

#define REG32(a) (*(volatile u32 *)(a))
#define CELL ((volatile u32 *)0xb003ff10)
#define GUARD ((volatile u32 *)0xb003ff00)
#define MARK 0x50524531 /* "PRE1" */

static int row;

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

static char *put_hex(char *p, u32 v)
{
    int i;
    for (i = 7; i >= 0; i--)
        *p++ = "0123456789ABCDEF"[v >> (4 * i) & 15];
    return p;
}

static void show(const char *name, u32 v, int hex)
{
    char text[48], *p = text;
    while (*name)
        *p++ = *name++;
    *p++ = ' ';
    p = hex ? put_hex(p, v) : put_dec(p, v);
    *p = 0;
    screen_text(8, 30 + 18 * row++, text, 1, 2);
    console_puts(text);
    console_puts("\n");
}

static void rec(u8 item, u32 value, u32 second)
{
    u8 d[8] = { 0x32, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, (u8)(second >> 8), (u8)second };
    can_send_for(0x7f8, d, 8, 3000);
}

/* Manufacturer and device of the chip at `base`, as the stock loader
 * asks for them: the autoselect command, two reads, and back. */
static void identity(u32 base, u32 *maker, u32 *device)
{
    REG32(base + 0x1554) = 0xaa;
    REG32(base + 0x0aa8) = 0x55;
    REG32(base + 0x1554) = 0x90;
    *maker = REG32(base);
    *device = REG32(base + 4);
    REG32(base) = 0xf0;
}

int main(void)
{
    u32 run, back, maker, device, i;
    board_init();
    console_puts("\nrnse sdk: preflight\n");
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 200, 0);
    screen_text(8, 6, "BEFORE A PROGRAM GOES INTO FLASH", 2, 2);
    can_init();
    run = CELL[0] == MARK && CELL[1] == ~CELL[2] ? CELL[1] + 1 : 1;
    CELL[0] = MARK;
    CELL[1] = run;
    CELL[2] = ~run;
    back = CELL[0] == MARK && CELL[2] == ~CELL[1] ? CELL[1] : 0xffffffff;
    show("RUN", run, 0);
    show(back == run ? "MEMORY KEEPS IT:" : "MEMORY DOES NOT KEEP IT:", back, 0);
    rec(0, run, back == run);
    for (i = 0; i < 3; i++)
        rec((u8)(1 + i), GUARD[i], 0);
    show("GUARD'S MARK", GUARD[0], 1);
    show("GUARD'S COUNT", GUARD[1], 1);
    for (i = 0; i < 2; i++) {
        identity(0xa0000000 + i * 0x01000000, &maker, &device);
        rec((u8)(4 + i), maker, device & 0xffff);
        show(i ? "FLASH 2 MAKER" : "FLASH 1 MAKER", maker, 1);
        show(i ? "FLASH 2 DEVICE" : "FLASH 1 DEVICE", device, 1);
    }
    screen_text(8, screen_height - 20, "RESTARTS IN 15 S", 2, 2);
    delay_ms(15000);
    restart();
    return 0;
}
