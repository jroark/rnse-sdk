/* What the 193's radio processor says by itself (a key on it: POWER),
 * for the CAN push (sdload193 starts it; it takes the next push too).
 * No keep-alive; every frame it sends is acknowledged (20 [00], with
 * -DNO_ACK not) and goes out on CAN 0x7F8 as record 0xe6 (item: its command; value:
 * its first four data bytes; second: its length), the rest of a longer
 * one as 0xe7 records (item: the offset), and every 5 s a record 0xe6
 * 0xff with the time and whether the faceplate answers. */
#include "rnse.h"

#define BUFFER ((void *)0x8c000000)

static void rec(u8 test, u8 item, const u8 *v4, u16 second)
{
    u8 d[8] = { test, item, v4[0], v4[1], v4[2], v4[3], (u8)(second >> 8), (u8)second };
    can_send_for(0x7f8, d, 8, 3000);
}

static void can_frames(void)
{
    u16 id;
    u8 d[8];
    int len;
    while ((len = can_receive(&id, d)) >= 0)
        canload_frame(id, d, len);
}

int main(void)
{
    u8 data[64];
    u32 beat = 0, restarted = 0;
    int cmd, n, i;
    radio_keepalive_off = 1;
    board_init();
    console_puts("\nrnse sdk: radiolisten\n");
    screen_palette(1, 255, 255, 255);
    screen_fill(0, 0, screen_width, screen_height, 0);
    screen_text(16, 8, "RADIOLISTEN 3: FACEPLATE AGAIN WHEN THE RADIO SAYS ON", 1, 2);
    panel_init();
    can_attach();
    {
        /* (Start the faceplate controller again whenever the radio
         * processor reports the unit on, status byte 3 bit 4, and the
         * controller does not answer: 0xe8 records, value the time.) */
    }
    canload_init(BUFFER, 0x100000);
    sd_idle = panel_service;
    display_idle = panel_service;
    for (;;) {
        panel_service();
        can_frames();
        cmd = radio_listen(data, sizeof data, &n, 20);
        if (cmd == 0x88 && n > 3 && (data[3] & 0x10) && !panel_online() && millis() - restarted > 3000) {
            u8 t[4];
            u32 now = millis();
            restarted = now;
            t[0] = (u8)(now >> 24), t[1] = (u8)(now >> 16), t[2] = (u8)(now >> 8), t[3] = (u8)now;
            panel_init();
            rec(0xe8, 1, t, 0);
        }
        if (cmd >= 0) {
            u8 v[4] = { 0, 0, 0, 0 };
#ifdef ACK
            if (cmd != 0x20)
                radio_ack();
#endif
            for (i = 0; i < 4 && i < n; i++)
                v[i] = data[i];
            rec(0xe6, (u8)cmd, v, (u16)n);
            for (i = 4; i < n; i += 4) {
                u8 w[4] = { 0, 0, 0, 0 };
                int k;
                for (k = 0; k < 4 && i + k < n; k++)
                    w[k] = data[i + k];
                rec(0xe7, (u8)i, w, (u16)cmd);
            }
        }
        if (millis() - beat >= 5000) {
            u32 t = millis();
            u8 v[4] = { (u8)(t >> 24), (u8)(t >> 16), (u8)(t >> 8), (u8)t };
            beat = t;
            rec(0xe6, 0xff, v, (u16)panel_online());
        }
    }
    return 0;
}
