/* CAN on a bench with another node (a CANable, say, and the Mac's
 * tools/canbench.py): what arrives, what the controller thinks of the
 * bus, and whether frames sent back to back all get out.
 *
 * Every frame received is answered on 0x7f8 with
 *     'R' n idhi idlo len h0 h1 h2
 * (n: frames received so far, low byte; h: FNV-1a of its data, low 24
 * bits), so the far end can check each one without looking at the
 * screen.  Once a second it sends the stock software's status frame
 * (0x661) and its own state on 0x7f8:
 *     'S' gsrhi gsrlo tec rec losthi lostlo fails
 * and every ten seconds, or when 0x7f7 [01] arrives, a burst of sixteen
 * frames back to back: 'B' burst i.  0x7f7 [02] sets the counters to 0.
 * The keys work as in every program: the display key, CD EJECT, RETURN
 * held for two seconds back to the loader. */
#include "rnse.h"

#define ROWS 8
#define ECHO 0x7f8
#define ORDERS 0x7f7

static u32 received, fails, bursts, burst_fails;
static int row;

static char *put_hex(char *p, u32 v, int digits)
{
    while (digits--)
        *p++ = "0123456789ABCDEF"[v >> (4 * digits) & 15];
    return p;
}

static char *put_dec(char *p, u32 v)
{
    char t[11];
    int n = 0;
    do {
        t[n++] = (char)('0' + umod(v, 10));
        v = udiv(v, 10);
    } while (v);
    while (n)
        *p++ = t[--n];
    return p;
}

static void line(int y, const char *text, u8 colour)
{
    int s = screen_width / 400;
    screen_fill(8 * s, y * s, screen_width - 16 * s, 14 * s, 0);
    screen_text(8 * s, y * s, text, colour, 2 * s);
}

static void show(u16 id, const u8 *data, int len, u8 colour)
{
    char text[40], *p = text;
    int i;
    p = put_hex(p, id, 3);
    *p++ = ':';
    for (i = 0; i < len; i++) {
        *p++ = ' ';
        p = put_hex(p, data[i], 2);
    }
    *p = 0;
    line(52 + 20 * row, text, colour);
    row = (row + 1) % ROWS;
    console_puts(colour == 2 ? "tx " : "rx ");
    console_puts(text);
    console_puts("\n");
}

static int send(const u8 *d, int len)
{
    if (can_send(ECHO, d, len) == 0)
        return 0;
    fails++;
    return -1;
}

static void state(void)
{
    char text[48], *p = text;
    u32 st = can_status(), lost = can_lost();
    u8 d[8];
    d[0] = 'S';
    d[1] = (u8)(st >> 24);
    d[2] = (u8)(st >> 16);
    d[3] = (u8)(st >> 8);
    d[4] = (u8)st;
    d[5] = (u8)(lost >> 8);
    d[6] = (u8)lost;
    d[7] = (u8)fails;
    send(d, 8);
    p = put_hex(p, st >> 16, 4);
    *p++ = ' ';
    *p++ = 'T';
    p = put_dec(p, st >> 8 & 0xff);
    *p++ = ' ';
    *p++ = 'R';
    p = put_dec(p, st & 0xff);
    *p++ = ' ';
    *p++ = 'L';
    p = put_dec(p, lost);
    *p++ = ' ';
    *p++ = 'F';
    p = put_dec(p, fails);
    *p++ = ' ';
    *p++ = 'N';
    p = put_dec(p, received);
    *p = 0;
    line(6, text, 1);
}

static void burst(void)
{
    char text[40], *p = text;
    u8 d[3];
    int i, bad = 0;
    bursts++;
    for (i = 0; i < 16; i++) {
        d[0] = 'B';
        d[1] = (u8)bursts;
        d[2] = (u8)i;
        if (send(d, 3))
            bad++;
    }
    burst_fails += (u32)bad;
    p = put_dec(p, bursts);
    *p++ = ' ';
    *p++ = 'B';
    *p++ = 'U';
    *p++ = 'R';
    *p++ = 'S';
    *p++ = 'T';
    *p++ = 'S';
    *p++ = ' ';
    p = put_dec(p, burst_fails);
    *p++ = ' ';
    *p++ = 'M';
    *p++ = 'I';
    *p++ = 'S';
    *p++ = 'S';
    *p++ = 'E';
    *p++ = 'D';
    *p = 0;
    line(28, text, bad ? 2 : 1);
}

int main(void)
{
    static const u8 status[8] = { 0x83, 0x01, 0x12, 0xa0, 0, 0, 0, 0 };
    u32 second, ten;

    board_init();
    console_puts("\nrnse sdk: canbench\n");
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 96, 0);
    screen_palette(3, 120, 120, 120);
    panel_init();
    can_init();
    line(6, "CAN BENCH", 1);
    second = millis() + 1000;
    ten = millis() + 2000;
    for (;;) {
        u16 id;
        u8 data[8];
        int len;
        panel_service();
        len = can_receive(&id, data);
        if (len >= 0) {
            u32 h = 2166136261u;
            u8 d[8];
            int i;
            for (i = 0; i < len; i++)
                h = (h ^ data[i]) * 16777619u;
            received++;
            d[0] = 'R';
            d[1] = (u8)received;
            d[2] = (u8)(id >> 8);
            d[3] = (u8)id;
            d[4] = (u8)len;
            d[5] = (u8)(h >> 16);
            d[6] = (u8)(h >> 8);
            d[7] = (u8)h;
            send(d, 8);
            show(id, data, len, 3);
            if (id == ORDERS && len >= 1 && data[0] == 1)
                ten = millis();
            if (id == ORDERS && len >= 1 && data[0] == 2)
                received = fails = bursts = burst_fails = 0;
            continue;
        }
        if ((s32)(millis() - second) >= 0) {
            second += 1000;
            if (can_send(0x661, status, 8) == 0)
                show(0x661, status, 8, 2);
            else
                fails++;
            state();
        } else if ((s32)(millis() - ten) >= 0) {
            ten = millis() + 10000;
            burst();
        } else {
            cpu_idle();
        }
    }
}
