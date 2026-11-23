/* The 193's faceplate controller, on its own: started as the stock
 * software starts it (its reset line the only bit of its port touched,
 * src/hw193.c), given its settings and its lighting, which is what
 * lights the screen (rnse docs/hardware.md: dark from a reset of the
 * controller until a lighting command), and listened to for a minute.
 * Every change of the keys and knobs goes out on CAN; none of them is
 * acted on: no eject, no display motor (the kit's earlier faceplate
 * start on the 193 left the drive ejecting and loading).  Then the unit
 * starts again.  For the SD loader (examples/sdload193).
 *
 * Records on 0x7F8 (test, item, value, second), through the CAN
 * controller as the stock software left it:
 *
 *   0x80 0     alive; 0x80 1 board_init done
 *   0x81 0     panel_online, ms after panel_init (0 while not yet)
 *   0x82 0     the 32 bits of the first key bytes (value), 32..47 (second)
 *   0x83 k     knob k turned: value = detents since the start
 *   0x84 0     every second: ms, second = online
 *   0x85 0     the faceplate port's registers after the start: +0, +4
 *   0xff 0xff  the end; the unit starts again */
#include "rnse.h"

#define CAN 0xffdf0000
#define CAN_MB1 (CAN + 0x100 + 0x20)

static void say(u8 test, u8 item, u32 value, u32 second)
{
    u8 d[8] = { test, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, (u8)(second >> 8), (u8)second };
    can_tell(0x7f8, d, 8, 0);
}

static void bars(void)
{
    static const u8 colours[8][3] = {
        { 255, 255, 255 }, { 255, 255, 0 }, { 0, 255, 255 }, { 0, 255, 0 },
        { 255, 0, 255 },   { 255, 0, 0 },   { 0, 0, 255 },   { 40, 40, 40 },
    };
    int i, w = screen_width / 8;
    for (i = 0; i < 8; i++) {
        screen_palette(10 + i, colours[i][0], colours[i][1], colours[i][2]);
        screen_fill(i * w, 0, w, screen_height * 2 / 3, (u8)(10 + i));
    }
    screen_palette(1, 255, 255, 255);
    screen_text(16, screen_height * 2 / 3 + 16, "PANEL 193: KEYS AND KNOBS ON CAN", 1, 2);
}

int main(void)
{
    u32 start, next = 0, keys = 0xffffffff, more = 0xffffffff, online_at = 0;
    int k, knob[2] = { 0, 0 };

    REG32(CAN + 0x28) = 0xfffffffe; /* the stock software's queued frames taken back */
    say(0x80, 0, 0, 0);
    board_init();
    say(0x80, 1, millis(), 0);
    bars();

    panel_init();
    say(0x85, 0, REG32(0xff909000), (u16)REG32(0xff909004));
    start = millis();
    while (millis() - start < 60000) {
        u32 now = 0, now2 = 0;
        panel_poll();
        if (!online_at && panel_online()) {
            online_at = millis() - start;
            say(0x81, 0, online_at, 0);
        }
        for (k = 0; k < 32; k++)
            now |= (u32)(panel_key(k) != 0) << k;
        for (k = 32; k < 48; k++)
            now2 |= (u32)(panel_key(k) != 0) << (k - 32);
        if (now != keys || now2 != more) {
            keys = now;
            more = now2;
            say(0x82, 0, keys, more);
            screen_fill(16, screen_height * 2 / 3 + 48, 600, 20, 0);
            screen_text(16, screen_height * 2 / 3 + 48, keys || more ? "KEY DOWN" : "NO KEY", 1, 2);
        }
        for (k = 0; k < 2; k++) {
            int t = panel_knob(k);
            if (t) {
                knob[k] += t;
                say(0x83, (u8)k, (u32)knob[k], 0);
            }
        }
        if ((s32)(millis() - next) >= 0) {
            next = millis() + 1000;
            say(0x84, 0, millis(), (u32)panel_online());
        }
        cpu_idle();
    }
    say(0xff, 0xff, millis(), 0);
    restart();
    return 0;
}
