/* Received CAN frames, from the interrupt to the program: a ring of 128
 * with one writer (the interrupt) and one reader (enough for a window
 * of canload's frames, below, while the program is busy elsewhere). */
#include "rnse.h"

#define RING 128

static struct {
    u16 id;
    u8 len;
    u8 data[8];
} ring[RING];
static volatile u32 head; /* where the interrupt writes next */
static volatile u32 tail; /* where the program reads next */
static volatile u32 lost;

void can_interrupt(void)
{
    u16 id;
    u8 data[8];
    int len, i;
    while ((len = can_take(&id, data)) != -1) {
        u32 next = (head + 1) & (RING - 1);
        if (len < 0 || next == tail) {
            lost++;
            continue;
        }
        ring[head].id = id;
        ring[head].len = (u8)len;
        for (i = 0; i < len; i++)
            ring[head].data[i] = data[i];
        head = next;
    }
}

int can_receive(u16 *id, u8 *data)
{
    int len, i;
    if (tail == head)
        return -1;
    *id = ring[tail].id;
    len = ring[tail].len;
    for (i = 0; i < len; i++)
        data[i] = ring[tail].data[i];
    tail = (tail + 1) & (RING - 1);
    return len;
}

u32 can_lost(void)
{
    return lost;
}

/* ---- canload: a new program over the bus ---------------------------
 *
 * For working on a unit without swapping cards or discs (the 193 needs
 * its PIN after every reset): tools/canbench.py run ... --push FILE
 * sends a program's image, and a program that takes part (canload_init,
 * then every received frame through canload_frame) puts it in RAM,
 * checks it and starts it with run_image, as the SD loader would have.
 *
 *   0x7f0 [01 s2 s1 s0 c3 c2 c1 c0]  start: size (24 bits), CRC-32
 *   0x7f1 [n d0 .. d6]               data: frame number n (mod 256), seven bytes
 *   0x7f0 [03]                       end: check and start
 *   0x7f0 [04]                       abort
 *
 * Answers on 0x7f8 (the bench programs' records):
 *   c0 01 size       ready for the data (c0 02: too big, c0 03: not now)
 *   c1 00 offset     every 32 frames, and at the end: had all up to offset
 *   c1 ff offset n   a frame missing: send again from offset (frame n)
 *   c2 00 crc        checked, starting it; c2 01 crc: the CRC is wrong;
 *                    c2 02: it did not start (run_image refused it) */
#define LOAD_CTL 0x7f0
#define LOAD_DATA 0x7f1
#define LOAD_WINDOW 32

int (*canload_start)(void *image, u32 bytes);
static u8 *load_buf;
static u32 load_max, load_size, load_crc, load_off, load_frames;
static int load_on, load_nak;

static void load_say(u8 a, u8 b, u32 v, u16 w)
{
    u8 d[8] = { a, b, (u8)(v >> 24), (u8)(v >> 16), (u8)(v >> 8), (u8)v, (u8)(w >> 8), (u8)w };
    can_send_for(0x7f8, d, 8, 3000);
}

static u32 load_crc32(const u8 *p, u32 n)
{
    u32 c = 0xffffffff;
    int k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xedb88320 & (0 - (c & 1)));
    }
    return ~c;
}

void canload_init(void *buffer, u32 max)
{
    load_buf = buffer;
    load_max = max;
    load_on = 0;
}

int canload_frame(u16 id, const u8 *d, int len)
{
    u32 i, n;
    if (!load_buf || (id != LOAD_CTL && id != LOAD_DATA))
        return 0;
    if (id == LOAD_CTL && len >= 1) {
        if (d[0] == 0x01 && len >= 8) {
            load_size = (u32)d[1] << 16 | (u32)d[2] << 8 | d[3];
            load_crc = (u32)d[4] << 24 | (u32)d[5] << 16 | (u32)d[6] << 8 | d[7];
            load_off = load_frames = 0;
            load_nak = 0;
            load_on = load_size <= load_max && load_size >= 0x20;
            load_say(0xc0, load_on ? 0x01 : 0x02, load_size, 0);
        } else if (d[0] == 0x03 && load_on) {
            u32 crc;
            load_on = 0;
            crc = load_crc32(load_buf, load_size);
            if (load_off < load_size || crc != load_crc) {
                load_say(0xc2, 0x01, crc, 0);
                return 1;
            }
            load_say(0xc2, 0x00, crc, 0);
            if (loader_leave)
                loader_leave();
            if (canload_start)
                canload_start(load_buf, load_size);
            else
                run_image(load_buf, load_size);
            load_say(0xc2, 0x02, crc, 0);
        } else if (d[0] == 0x04) {
            load_on = 0;
        }
        return 1;
    }
    if (!load_on || len < 2)
        return 1;
    if (d[0] != (u8)load_frames) {
        /* One went missing (or this is one sent again after it): from
         * the first that did not come, once per window. */
        if ((u8)(d[0] - (u8)load_frames) < 0x80 && !load_nak) {
            load_nak = 1;
            load_say(0xc1, 0xff, load_off, (u16)load_frames);
        }
        return 1;
    }
    load_nak = 0;
    n = (u32)len - 1;
    for (i = 0; i < n && load_off < load_size; i++)
        load_buf[load_off++] = d[1 + i];
    load_frames++;
    if (load_frames % LOAD_WINDOW == 0 || load_off >= load_size)
        load_say(0xc1, 0x00, load_off, (u16)load_frames);
    return 1;
}

void canload_poll(void)
{
    u16 id;
    u8 d[8];
    int len;
    while ((len = can_receive(&id, d)) >= 0)
        canload_frame(id, d, len);
}
