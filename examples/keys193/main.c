/* Ejects the disc of a 193 first thing (the drive commands of
 * examples/eject193, before anything else runs), then logs on CAN every
 * message the faceplate controller sends, for 40 s, so that the keys
 * the kit does not know on the 193 can be found: on the bench 193 CD
 * EJECT did nothing under the kit (2026-11-21) and its bit (31) never
 * came in examples/panel193's log.  No key is acted on.  Then the unit
 * starts again.
 *
 * Records on 0x7F8 (test, item, value, second):
 *   0xc0 0     alive; 0xc0 1 the eject's result (0 done, 1..4 the step
 *              that timed out, 0x100+ the drive's error)
 *   0x86 t     a message of type t not like the one before of its type:
 *              value its data bytes 0-3, second bytes 4-5; 0x87 t its
 *              bytes 6-9 (value) and its length (second), when longer
 *   0x82 0     the kit's 48 key bits as they change (value bits 0-31,
 *              second 32-47)
 *   0x84 0     every 2 s: ms, second = online
 *   0xff 0xff  the end */
#include "rnse.h"

#define CAN 0xffdf0000
#define CAN_MB1 (CAN + 0x100 + 0x20)

static void say(u8 test, u8 item, u32 value, u32 second)
{
    u8 d[8] = { test, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, (u8)(second >> 8), (u8)second };
    can_tell(0x7f8, d, 8, 0);
}

/* The drive's status with a counted wait for the bits to clear. */
static int clear(u8 bits)
{
    u32 n;
    for (n = 0; n < 20000000; n++)
        if (!(cd_alt_status() & bits))
            return 0;
    return -1;
}

/* One packet command without data, as examples/probe193 sent it. */
static int packet(const u8 *command)
{
    int i;
    if (clear(0x80))
        return 1;
    cd_reg_write(6, 0xa0);
    if (clear(0x88))
        return 2;
    cd_reg_write(1, 0x00);
    cd_reg_write(4, 0x00);
    cd_reg_write(5, 0x08);
    cd_reg_write(7, 0xa0);
    cd_alt_status();
    if (clear(0x80))
        return 3;
    if (!(cd_alt_status() & 0x08))
        return 0x100 | cd_reg_read(1);
    for (i = 0; i < 12; i += 2)
        cd_data_write((u16)(command[i] << 8 | command[i + 1]));
    cd_alt_status();
    if (clear(0x80))
        return 4;
    if (cd_reg_read(7) & 0x01)
        return 0x100 | cd_reg_read(1);
    return 0;
}

static u8 last[32][12];
static u8 last_n[32];

static void frame_seen(int type, const u8 *d, int n)
{
    int i, same;
    u8 t = (u8)type & 31;
    same = n == last_n[t];
    for (i = 0; same && i < n && i < 12; i++)
        same = d[i] == last[t][i];
    if (same)
        return;
    last_n[t] = (u8)n;
    for (i = 0; i < n && i < 12; i++)
        last[t][i] = d[i];
    {
        u8 b[10] = { 0 };
        for (i = 0; i < n && i < 10; i++)
            b[i] = d[i];
        say(0x86, (u8)type, (u32)b[0] << 24 | (u32)b[1] << 16 | (u32)b[2] << 8 | b[3], (u32)b[4] << 8 | b[5]);
        if (n > 6)
            say(0x87, (u8)type, (u32)b[6] << 24 | (u32)b[7] << 16 | (u32)b[8] << 8 | b[9], (u32)n);
    }
}

int main(void)
{
    static const u8 allow_removal[12] = { 0x1e, 0, 0, 0, 0 };
    static const u8 eject[12] = { 0x1b, 1, 0, 0, 2 };
    u32 start, next = 0, keys = 0xffffffff, more = 0xffffffff;
    int k, r;

    REG32(CAN + 0x28) = 0xfffffffe;
    say(0xc0, 0, 0, 0);
    /* The disc first, before anything else can go wrong. */
    r = packet(allow_removal);
    if (r == 0)
        r = packet(eject);
    say(0xc0, 1, (u32)r, 0);

    board_init();
    screen_palette(1, 255, 255, 255);
    screen_text(16, 16, r == 0 ? "THE DISC IS OUT: TAKE IT" : "THE DRIVE DID NOT EJECT", 1, 3);
    screen_text(16, 64, "PRESS CD EJECT, THEN THE DISPLAY KEY, A FEW TIMES", 1, 2);
    panel_frame_hook = frame_seen;
    panel_init();
    start = millis();
    while (millis() - start < 40000) {
        u32 now = 0, now2 = 0;
        panel_poll();
        for (k = 0; k < 32; k++)
            now |= (u32)(panel_key(k) != 0) << k;
        for (k = 32; k < 48; k++)
            now2 |= (u32)(panel_key(k) != 0) << (k - 32);
        if (now != keys || now2 != more) {
            keys = now;
            more = now2;
            say(0x82, 0, keys, more);
        }
        if ((s32)(millis() - next) >= 0) {
            next = millis() + 2000;
            say(0x84, 0, millis(), (u32)panel_online());
        }
        cpu_idle();
    }
    say(0xff, 0xff, millis(), 0);
    restart();
    return 0;
}
