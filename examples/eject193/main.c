/* Ejects the disc of a 193 that runs the SD loader from it, for a bench
 * where nobody is at the keys: pushed to the SD bridge, it runs at the
 * loader's next start.  The drive is told to let the disc go and eject
 * it, as examples/probe193 did on the unit; the faceplate is started so
 * that the screen lights (examples/panel193) and says to take the disc;
 * no key is acted on.  After 20 seconds the unit starts again, with its
 * stock software when the disc is out.
 *
 * Records on 0x7F8: 0xc0 0 alive; 0xc0 1 the eject's result (0 done,
 * 1..4 the step that timed out, 0x100+ the drive's error); 0xff 0xff the
 * end. */
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

int main(void)
{
    static const u8 allow_removal[12] = { 0x1e, 0, 0, 0, 0 };
    static const u8 eject[12] = { 0x1b, 1, 0, 0, 2 };
    u32 end;
    int r;

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
    screen_text(16, 64, "THE UNIT STARTS AGAIN IN 20 S", 1, 2);
    panel_init();
    end = millis() + 20000;
    while ((s32)(millis() - end) < 0) {
        panel_poll(); /* the lighting; no key is acted on */
        cpu_idle();
    }
    say(0xff, 0xff, millis(), 0);
    restart();
    return 0;
}
