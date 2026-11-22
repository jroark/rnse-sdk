/* A monitor for a real 193 on a bench: pushed over CAN once (the SD
 * loader's canload, tools/canbench.py --push), it stays and does what
 * frames on the bus ask of it, so that looking at a register, a port
 * or a stretch of memory takes a line of a script and not a program
 * built, pushed and decoded.  tools/bench193.py is the other end, and
 * runs the same script on the emulator to compare.
 *
 * Requests: frame 0x7F2, eight bytes: seq, op, six bytes of the op's.
 * Answers: frame 0x7F3, eight bytes: seq, status, value (four bytes),
 * two bytes of the op's.  Status 0 done, 1 an exception was taken
 * (value: EXPEVT), 2 refused, 3 not understood.
 *
 *   01 READ   address (4), size (1, 2 or 4), count (0 = 1): an answer
 *             for each, the value, then the index and the size; it
 *             stops at the first exception
 *   02 ARG    a value (4): kept for WRITE
 *   03 WRITE  address (4), size: ARG is written there.  Refused for the
 *             flash (area 0) and the battery-backed memory (area 6),
 *             which this program never writes
 *   04 BLOCK  address (4), length (2): the bytes on frames 0x7F9 (three
 *             bytes of offset, five of data), then the answer: the
 *             CRC-32 of them all, and the length
 *   05 INFO   item (1): 0 this program's version, 1 the clock in ms,
 *             2 SR, 3 VBR, 4 the front (bit 0 the faceplate answers,
 *             bits 8-15 the display's state, bits 16-31 its position),
 *             5 CAN frames lost, 6 in a loader's session, 7 exceptions
 *             taken so far, 8 the last one's SPC, 9 frames of its own
 *             that did not go out (high half) and sends tried again
 *   06 RESTART  "RST!": the unit is started again
 *   07 HANG     "HANG": this program stops serving anything, as one that
 *             hangs would: the watchdog must then start the unit again
 *   08 FRONT  1 or 0: the faceplate's messages on frames 0x7F4 (type,
 *             length, six bytes of data) as they come
 *   09 RADIO  1 or 0: what the radio processor sends unasked on frames
 *             0x7F5 (command, length, six bytes; more of a longer one
 *             on 0x7F6: offset, length, six bytes).  Nothing is sent
 *             to it
 *   0a LOADER   "BACK": back to the loader (as RETURN held)
 *   0c KEEPALIVE  1 or 0: the radio processor's keep-alive sent or not
 *             (it is, from the start: without it the unit starts again
 *             after some thirteen minutes)
 *   0d CANINIT  variant (1): the CAN controller from reset (can_try,
 *             below); the answer: the status after the reset and when
 *             running, the variant, the tries the answer took.  No
 *             answer: that variant leaves the bus silent, and the unit
 *             starts again
 *   0b TORADIO  command (1), length (1, up to 4), data: a frame to the
 *             radio processor (radio_command); the answer's first four
 *             bytes, then radio_command's result and the answer's
 *             command (0x20: an acknowledgement)
 *
 * A record on 0x7F8 at the start (0xb1, version, SR) and every two
 * seconds (0xb0, item: bit 0 the faceplate answers, bits 1-3 the
 * display's state; value: the clock; second: requests done).
 *
 * The watchdog is strobed from the main loop here, not from the tick:
 * a request that hangs the unit ends in the watchdog's reset and the
 * stock software's start, from where the disc's loader and a push
 * bring the monitor back with nobody at the bench.  The display key,
 * CD EJECT and RETURN held are followed wherever it waits
 * (panel_service), and a program pushed over CAN replaces it. */
#include "rnse.h"

#define VERSION 5
#define REQUEST 0x7f2
#define ANSWER 0x7f3
#define FRONT 0x7f4
#define RADIO 0x7f5
#define RADIO_MORE 0x7f6
#define BYTES 0x7f9
#define WATCHDOG 0xffc44008
#define BUFFER ((void *)0x8c000000)

/* A read or a write that may end in an exception: 0, or the EXPEVT of
 * the exception it took.  op: 0-2 read 1, 2, 4 bytes into *value, 3-5
 * write *value.  The exception handler (the kit's unexpected) calls
 * fault_hook below, which comes back through mon_landing with the
 * stack and the registers of the call. */
int mon_probe(u32 addr, u32 *value, int op);
void mon_landing(void);
volatile u32 mon_sp, mon_armed, mon_expevt, mon_spc, mon_faults;

__asm__(
    "        .text\n"
    "        .align  2\n"
    "        .global mon_probe\n"
    "mon_probe:\n"
    "        mov.l   r8, @-r15\n"
    "        mov.l   r9, @-r15\n"
    "        mov.l   r10, @-r15\n"
    "        mov.l   r11, @-r15\n"
    "        mov.l   r12, @-r15\n"
    "        mov.l   r13, @-r15\n"
    "        mov.l   r14, @-r15\n"
    "        sts.l   pr, @-r15\n"
    "        mov.l   8f, r0\n"
    "        mov.l   r15, @r0\n"
    "        mov.l   9f, r1\n"
    "        mov     #1, r0\n"
    "        mov.l   r0, @r1\n"
    "        mov     r6, r0\n"
    "        cmp/eq  #0, r0\n"
    "        bt      1f\n"
    "        cmp/eq  #1, r0\n"
    "        bt      2f\n"
    "        cmp/eq  #2, r0\n"
    "        bt      3f\n"
    "        mov.l   @r5, r2\n"
    "        cmp/eq  #3, r0\n"
    "        bt      4f\n"
    "        cmp/eq  #4, r0\n"
    "        bt      5f\n"
    "        mov.l   r2, @r4\n"
    "        bra     6f\n"
    "         nop\n"
    "4:      mov.b   r2, @r4\n"
    "        bra     6f\n"
    "         nop\n"
    "5:      mov.w   r2, @r4\n"
    "        bra     6f\n"
    "         nop\n"
    "1:      mov.b   @r4, r2\n"
    "        extu.b  r2, r2\n"
    "        bra     7f\n"
    "         nop\n"
    "2:      mov.w   @r4, r2\n"
    "        extu.w  r2, r2\n"
    "        bra     7f\n"
    "         nop\n"
    "3:      mov.l   @r4, r2\n"
    "7:      mov.l   r2, @r5\n"
    "6:      mov     #0, r0\n"
    "mon_out:\n"
    "        mov.l   9f, r1\n"
    "        mov     #0, r2\n"
    "        mov.l   r2, @r1\n"
    "        lds.l   @r15+, pr\n"
    "        mov.l   @r15+, r14\n"
    "        mov.l   @r15+, r13\n"
    "        mov.l   @r15+, r12\n"
    "        mov.l   @r15+, r11\n"
    "        mov.l   @r15+, r10\n"
    "        mov.l   @r15+, r9\n"
    "        mov.l   @r15+, r8\n"
    "        rts\n"
    "         nop\n"
    "        .global mon_landing\n"
    "mon_landing:\n"
    "        mov.l   8f, r0\n"
    "        mov.l   @r0, r15\n"
    "        mov.l   10f, r0\n"
    "        mov.l   @r0, r0\n"
    "        bra     mon_out\n"
    "         nop\n"
    "        .align  2\n"
    "8:      .long   mon_sp\n"
    "9:      .long   mon_armed\n"
    "10:     .long   mon_expevt\n");

static void fault_hook(u32 expevt, u32 intevt, u32 spc)
{
    (void)intevt;
    if (!mon_armed || !expevt)
        return;
    mon_expevt = expevt;
    mon_spc = spc;
    mon_faults++;
    /* SSR is still the probe's SR: back there, at the landing. */
    __asm__ volatile("ldc %0, spc\n\trte\n\tnop" : : "r"((u32)mon_landing));
}

static u32 strobe, requests;
static int front_on, radio_on;

static void feed(void)
{
    strobe ^= 1;
    REG32(WATCHDOG) = (REG32(WATCHDOG) & ~1u) | strobe;
}

/* A frame out: tried three times, and counted when it did not go (a
 * frame in thirty did not arrive from a bench unit; whether this end
 * lost it, the count says). */
static u32 tx_failed, tx_again;

static void tx(u16 id, const u8 *d)
{
    int i;
    for (i = 0; i < 3; i++) {
        if (can_send_for(id, d, 8, 5000) == 0)
            return;
        tx_again++;
        feed();
    }
    tx_failed++;
}

static void out(u16 id, u8 a, u8 b, u32 v, u8 x, u8 y)
{
    u8 d[8] = { a, b, (u8)(v >> 24), (u8)(v >> 16), (u8)(v >> 8), (u8)v, x, y };
    tx(id, d);
    feed();
}

static u32 be32(const u8 *p)
{
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

/* The flash and the battery-backed memory, whatever the address space
 * they are named through. */
static int guarded(u32 addr, u32 size)
{
    u32 first, last;
    if (addr >= 0xe0000000u)
        return 0;
    first = addr & 0x1fffffffu;
    last = first + size - 1;
    return first < 0x04000000u || (last >= 0x18000000u && first < 0x1c000000u);
}

static void front_frame(int type, const u8 *data, int n)
{
    u8 d[8] = { (u8)type, (u8)n, 0, 0, 0, 0, 0, 0 };
    int i;
    if (!front_on)
        return;
    for (i = 0; i < 6 && i < n; i++)
        d[2 + i] = data[i];
    tx(FRONT, d);
}

static u32 front(void)
{
    return (u32)(panel_online() & 1) | (u32)(display_state() & 0xff) << 8 | (u32)(display_position() & 0xffff) << 16;
}

static void service(void);

static void block(u8 seq, u32 addr, u32 len)
{
    u32 off, crc = 0xffffffff, v;
    int e, k;
    for (off = 0; off < len; off += 5) {
        u8 d[8] = { (u8)(off >> 16), (u8)(off >> 8), (u8)off, 0, 0, 0, 0, 0 };
        u32 i;
        for (i = 0; i < 5 && off + i < len; i++) {
            e = mon_probe(addr + off + i, &v, 0);
            if (e) {
                out(ANSWER, seq, 1, (u32)e, (u8)((off + i) >> 8), (u8)(off + i));
                return;
            }
            d[3 + i] = (u8)v;
            crc ^= v;
            for (k = 0; k < 8; k++)
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
        tx(BYTES, d);
        feed();
        if ((off & 0x3f) < 5)
            service();
    }
    out(ANSWER, seq, 0, ~crc, (u8)(len >> 8), (u8)len);
}

/* The CAN controller set up from reset, as the kit's can_init does it
 * (which left a bench 193's bus silent), with what may be wrong with
 * that made a choice: bit 0, the control register's top bit kept set,
 * as the stock software has it (it reads 0x8000 there, and can_init
 * writes 1 and 0); bit 1, the controller's own word waited for after
 * asking for the reset and after letting it run (status bit 3).  The
 * answer says which; if it cannot be sent in ten tries, or no request
 * comes in during the five seconds after it, the unit is started
 * again, which is the way back to a monitor that can talk. */
#define CAN 0xffdf0000
#define CAN_MB(n) (CAN + 0x100 + 0x20 * (n))

static u32 on_trial;

static void can_try(u8 seq, int variant)
{
    u16 top = variant & 1 ? 0x8000 : 0;
    u32 sr, t;
    u16 status_reset, status_run;
    int n, ok = 0;
    sr = irq_save();
    REG16(CAN + 0x00) = (u16)(top | 0x0001);
    if (variant & 2)
        for (t = micros(); !(REG16(CAN + 0x02) & 0x0008) && micros() - t < 100000;)
            ;
    status_reset = REG16(CAN + 0x02);
    REG16(CAN + 0x08) = 0x0001;
    REG16(CAN + 0x0a) = 0xfffd;
    for (n = 0; n < 32; n++) {
        REG32(CAN_MB(n) + 0x00) = 0;
        REG32(CAN_MB(n) + 0x08) = 0;
        REG32(CAN_MB(n) + 0x0c) = 0;
        REG32(CAN_MB(n) + 0x10) = 0;
        REG16(CAN_MB(n) + 0x10) = 0x0700;
    }
    REG16(CAN_MB(1) + 0x10) = 0x0000;
    REG16(CAN_MB(24) + 0x10) = 0x0300;
    REG16(CAN_MB(24) + 0x04) = 0x1ffc;
    REG16(CAN + 0x06) = 0x000b;
    REG16(CAN + 0x04) = 0xb730;
    REG16(CAN + 0x00) = top;
    if (variant & 2)
        for (t = micros(); (REG16(CAN + 0x02) & 0x0008) && micros() - t < 100000;)
            ;
    status_run = REG16(CAN + 0x02);
    REG16(CAN + 0x50) = (u16)~(1 << (24 - 16));
    REG16(CAN + 0x52) = 0xffff;
    irq_restore(sr);
    for (n = 0; n < 10 && !ok; n++) {
        u8 d[8] = { seq, 0, (u8)(status_reset >> 8), (u8)status_reset, (u8)(status_run >> 8), (u8)status_run, (u8)variant, (u8)n };
        ok = can_send_for(ANSWER, d, 8, 20000) == 0;
        feed();
    }
    if (!ok)
        restart();
    /* A frame out proves little: a bench unit set up with variant 2 sent
     * its frames under other identifiers, heard none of ours, and had to
     * be switched off.  So a request has to come in within five seconds
     * of this, or the unit is started again. */
    on_trial = millis() | 1;
}

static void request(const u8 *d)
{
    static u32 arg;
    u8 seq = d[0];
    u32 a = be32(d + 2), v;
    int size = d[6], i, e, n;
    requests++;
    switch (d[1]) {
    case 0x01:
        if (size != 1 && size != 2 && size != 4) {
            out(ANSWER, seq, 3, 0, 0, 0);
            break;
        }
        n = d[7] ? d[7] : 1;
        for (i = 0; i < n; i++) {
            e = mon_probe(a + (u32)(i * size), &v, size == 1 ? 0 : size == 2 ? 1 : 2);
            out(ANSWER, seq, e ? 1 : 0, e ? (u32)e : v, (u8)i, (u8)size);
            if (e)
                break;
            if ((i & 15) == 15)
                service();
        }
        break;
    case 0x02:
        arg = a;
        out(ANSWER, seq, 0, arg, 0, 0);
        break;
    case 0x03:
        if (size != 1 && size != 2 && size != 4) {
            out(ANSWER, seq, 3, 0, 0, 0);
            break;
        }
        if (guarded(a, (u32)size)) {
            out(ANSWER, seq, 2, a, 0, (u8)size);
            break;
        }
        v = arg;
        e = mon_probe(a, &v, size == 1 ? 3 : size == 2 ? 4 : 5);
        out(ANSWER, seq, e ? 1 : 0, e ? (u32)e : arg, 0, (u8)size);
        break;
    case 0x04:
        block(seq, a, (u32)d[6] << 8 | d[7]);
        break;
    case 0x05:
        switch (d[2]) {
        case 0: v = 0x4d4f4e00u | VERSION; break;
        case 1: v = millis(); break;
        case 2: __asm__ volatile("stc sr, %0" : "=r"(v)); break;
        case 3: __asm__ volatile("stc vbr, %0" : "=r"(v)); break;
        case 4: v = front(); break;
        case 5: v = can_lost(); break;
        case 6: v = (u32)loader_active(); break;
        case 7: v = mon_faults; break;
        case 8: v = mon_spc; break;
        case 9: v = tx_failed << 16 | (tx_again & 0xffff); break;
        default:
            out(ANSWER, seq, 3, 0, d[2], 0);
            return;
        }
        out(ANSWER, seq, 0, v, d[2], 0);
        break;
    case 0x06:
        if (a != 0x52535421u) {
            out(ANSWER, seq, 2, 0, 0, 0);
            break;
        }
        out(ANSWER, seq, 0, a, 0, 0);
        restart();
        break;
    case 0x07:
        if (a != 0x48414e47u) {
            out(ANSWER, seq, 2, 0, 0, 0);
            break;
        }
        out(ANSWER, seq, 0, a, 0, 0);
        (void)irq_save();
        for (;;)
            ;
    case 0x08:
        front_on = d[2] & 1;
        out(ANSWER, seq, 0, (u32)front_on, 0, 0);
        break;
    case 0x09:
        radio_on = d[2] & 1;
        out(ANSWER, seq, 0, (u32)radio_on, 0, 0);
        break;
    case 0x0d:
        can_try(seq, d[2]);
        break;
    case 0x0c:
        radio_keepalive_off = !(d[2] & 1);
        out(ANSWER, seq, 0, (u32)!radio_keepalive_off, 0, 0);
        break;
    case 0x0b: {
        u8 answer[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        n = d[3] > 4 ? 4 : d[3];
        e = radio_command(d[2], d + 4, n, answer, sizeof answer);
        out(ANSWER, seq, 0, be32(answer), (u8)e, (u8)radio_answered);
        break;
    }
    case 0x0a:
        if (a != 0x4241434bu || !loader_active()) {
            out(ANSWER, seq, 2, 0, 0, 0);
            break;
        }
        out(ANSWER, seq, 0, a, 0, 0);
        loader_return(0);
        break;
    default:
        out(ANSWER, seq, 3, 0, d[1], 0);
    }
}

/* What is owed while anything waits: the watchdog, the keys. */
static void service(void)
{
    feed();
    panel_service();
}

static void radio_frames(void)
{
    u8 data[64], d[8];
    int cmd, n, i, k;
    cmd = radio_listen(data, sizeof data, &n, 0);
    if (cmd < 0)
        return;
    for (i = 0; i == 0 || i < n; i += 6) {
        d[0] = i ? (u8)i : (u8)cmd;
        d[1] = (u8)n;
        for (k = 0; k < 6; k++)
            d[2 + k] = i + k < n ? data[i + k] : 0;
        tx(i ? RADIO_MORE : RADIO, d);
        feed();
    }
}

int main(void)
{
    u32 sr, beat = 0;
    u16 id;
    u8 d[8];
    int len;

    __asm__ volatile("stc sr, %0" : "=r"(sr));
    board_init();
    /* From here on the watchdog is this loop's. */
    watchdog_hold = 1;
    feed();
    unexpected_hook = fault_hook;
    console_puts("\nrnse sdk: mon193\n");
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 160, 0);
    screen_fill(0, 0, screen_width, screen_height, 0);
    screen_text(16, 16, "MONITOR 193", 2, 2);
    screen_text(16, 40, "COMMANDS ON CAN.  RETURN HELD: THE LOADER", 1, 2);
    screen_text(16, screen_height - 24, "KEY BY THE POWER BUTTON: DISPLAY.  CD EJECT: DISC OUT", 2, 2);
    panel_frame_hook = front_frame;
    panel_init();
    /* The faceplate's start answered before anything else is done. */
    {
        u32 t = millis();
        while (millis() - t < 600) {
            service();
            cpu_idle();
        }
    }
    sd_idle = service;
    display_idle = service;
    can_attach();
    canload_init(BUFFER, 0x100000);
    out(0x7f8, 0xb1, VERSION, sr, 0, 0);
    for (;;) {
        service();
        while ((len = can_receive(&id, d)) >= 0) {
            if (id == REQUEST && len == 8) {
                on_trial = 0;
                request(d);
            }
            else
                canload_frame(id, d, len);
            feed();
        }
        if (on_trial && millis() - on_trial >= 5000)
            restart();
        if (radio_on)
            radio_frames();
        if (millis() - beat >= 2000) {
            beat = millis();
            out(0x7f8, 0xb0, (u8)((panel_online() & 1) | (display_state() & 7) << 1), beat, (u8)(requests >> 8), (u8)requests);
        }
        cpu_idle();
    }
    return 0;
}
