/* side193: a resident beside the 193's stock software.
 *
 * The stock software starts this from a disc as it would an update
 * loader; start.S copies it to RAM the stock software leaves alone and
 * returns, and from then on side_tick below runs ahead of the stock
 * software's own 1 ms tick handler, which it was put in front of in
 * the stock software's table of interrupt handlers (in RAM).  Nothing
 * else is taken from the stock software: the watchdog, the radio
 * processor, the faceplate, the display key and the disc's eject stay
 * its own, and nothing is written to flash or battery-backed memory.
 * docs/side193.md has what is measured and what is assumed.
 *
 * It talks on CAN as examples/mon193 does, so that tools/bench193.py
 * drives it with the same scripts: requests on 0x7F2 (seq, op, six
 * bytes), answers on 0x7F3 (seq, status, value, two bytes); status 0
 * done, 2 refused, 3 not understood.
 *
 *   01 READ   address (4), size (1, 2 or 4), count (0 = 1)
 *   02 ARG    a value (4): kept for WRITE
 *   03 WRITE  address (4), size: ARG is written there.  Refused for
 *             the flash and the battery-backed memory
 *   04 BLOCK  address (4), length (2): the bytes on 0x7F9 (three bytes
 *             of offset, five of data), then the CRC-32 and the length
 *   05 INFO   item: 0 version, 1 the clock in ms (ticks counted), 2 SR,
 *             3 VBR, 5 requests lost, 6 what this is (2: beside the
 *             stock software), 9 frames given up, 10 the stock tick
 *             handler's address, 11 the longest tick's work in timer
 *             counts
 *   10 OFF    "OFF!": the stock software's handler back in its table,
 *             and nothing more from here until the unit is started
 *             again
 *
 * There is no exception handler of its own here (the stock software's
 * would take a fault and start the unit again), so an address is
 * looked at before it is used: one that is not a multiple of the size,
 * or in the reserved top of the 29-bit space, is refused.  A register
 * address the chip does not have can still end in a restart.
 *
 * Two mailboxes of the CAN controller that the stock software leaves
 * unused (7 sends, 8 receives 0x7F0-0x7F3), their interrupts masked;
 * only their own bits in the controller's shared registers are
 * written.  A record on 0x7F8 at the start (0xb3, version, SR) and
 * every two seconds (0xb2, 0, the clock, requests done). */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define REG8(a) (*(volatile u8 *)(a))
#define REG16(a) (*(volatile u16 *)(a))
#define REG32(a) (*(volatile u32 *)(a))

#define VERSION 1
#define MARK 0x53494445u
#define REQUEST 0x7f2
#define ANSWER 0x7f3
#define RECORD 0x7f8
#define BYTES 0x7f9

#define CAN 0xffdf0000u
#define CAN_MB(n) (CAN + 0x100 + 0x20 * (n))
#define TX_MB 7
#define RX_MB 8
#define TX_BIT (1u << TX_MB)
#define RX_BIT (1u << RX_MB)
/* The low halves (mailboxes 15-0) of the controller's flag registers. */
#define TXPR0 (CAN + 0x22)
#define TXCR0 (CAN + 0x2a)
#define TXACK0 (CAN + 0x32)
#define ABACK0 (CAN + 0x3a)
#define RXPR0 (CAN + 0x42)
#define MBIMR0 (CAN + 0x52)
#define UMSR0 (CAN + 0x5a)

#define TCNT0 0xffd8000cu

void side_entry(void);
extern u32 _res_start[];

/* For start.S: the stack pointer of what the tick interrupted, the
 * resident's own stack, and the stock software's handler. */
u32 side_sp;
u32 side_stock_tick;
u32 side_stack[2048];

static u32 *slot;
static u32 canary = MARK;
static u32 ticks, requests, lost, given_up, longest, arg, off;

/* Frames waiting to go out, one a tick at most. */
#define QUEUE 64
static struct frame {
    u16 id;
    u8 d[8];
} queue[QUEUE];
static u32 q_in, q_out, sending_since;

/* A READ or a BLOCK under way: it goes on as the queue has room. */
static struct {
    u8 kind, seq, size;
    u32 addr, count, done, crc;
} job;

static u32 sr_get(void)
{
    u32 v;
    __asm__ volatile("stc sr, %0" : "=r"(v));
    return v;
}

static void sr_set(u32 v)
{
    __asm__ volatile("ldc %0, sr" : : "r"(v));
}

static u32 vbr_get(void)
{
    u32 v;
    __asm__ volatile("stc vbr, %0" : "=r"(v));
    return v;
}

static int room(void)
{
    return QUEUE - (q_in - q_out);
}

static void put(u16 id, const u8 *d)
{
    struct frame *f;
    int i;
    if (!room()) {
        given_up++;
        return;
    }
    f = &queue[q_in++ % QUEUE];
    f->id = id;
    for (i = 0; i < 8; i++)
        f->d[i] = d[i];
}

static void out(u16 id, u8 a, u8 b, u32 v, u8 x, u8 y)
{
    u8 d[8] = { a, b, (u8)(v >> 24), (u8)(v >> 16), (u8)(v >> 8), (u8)v, x, y };
    put(id, d);
}

/* The next frame, when the one before it has gone.  A frame that is
 * not out after 50 ms (nobody on the bus to acknowledge it) is taken
 * back, so that the mailbox does not stay asked for ever. */
static void pump(void)
{
    struct frame *f;
    int i;
    if (REG16(TXPR0) & TX_BIT) {
        if (ticks - sending_since > 50) {
            REG16(TXCR0) = TX_BIT;
            sending_since = ticks;
            given_up++;
        }
        return;
    }
    if (REG16(TXACK0) & TX_BIT)
        REG16(TXACK0) = TX_BIT;
    if (REG16(ABACK0) & TX_BIT)
        REG16(ABACK0) = TX_BIT;
    if (q_in == q_out)
        return;
    f = &queue[q_out++ % QUEUE];
    REG16(CAN_MB(TX_MB) + 0x00) = (u16)(f->id << 2);
    for (i = 0; i < 8; i++)
        REG8(CAN_MB(TX_MB) + 0x08 + i) = f->d[i];
    REG16(CAN_MB(TX_MB) + 0x10) = 8;
    REG16(TXPR0) = TX_BIT;
    sending_since = ticks;
}

static u32 be32(const u8 *p)
{
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

/* Whether an access of that size there can be made without the risk
 * of an exception this code knows of. */
static int sound(u32 addr, u32 size)
{
    if (size != 1 && size != 2 && size != 4)
        return 0;
    if (addr & (size - 1))
        return 0;
    if (addr < 0xe0000000u && (addr & 0x1fffffffu) >= 0x1c000000u)
        return 0;
    if (addr >= 0xe0000000u && addr < 0xfe000000u)
        return 0;
    return 1;
}

/* The flash and the battery-backed memory, whatever the address space
 * they are named through (as mon193 has it). */
static int guarded(u32 addr, u32 size)
{
    u32 first, last;
    if (addr >= 0xe0000000u)
        return 0;
    first = addr & 0x1fffffffu;
    last = first + size - 1;
    return first < 0x04000000u || (last >= 0x18000000u && first < 0x1c000000u);
}

static u32 peek(u32 addr, u32 size)
{
    return size == 1 ? REG8(addr) : size == 2 ? REG16(addr) : REG32(addr);
}

static void unhook(void)
{
    u32 sr = sr_get();
    sr_set(sr | 0xf0);
    if (slot && *slot == (u32)side_entry)
        *slot = side_stock_tick;
    sr_set(sr);
    off = 1;
}

static void request(const u8 *d)
{
    u8 seq = d[0];
    u32 addr = be32(d + 2);
    requests++;
    switch (d[1]) {
    case 0x01:
    case 0x04:
        if (job.kind) {
            out(ANSWER, seq, 2, 0, 0, 0);
            break;
        }
        job.seq = seq;
        job.addr = addr;
        job.done = 0;
        job.crc = 0xffffffff;
        if (d[1] == 0x01) {
            job.size = d[6];
            job.count = d[7] ? d[7] : 1;
            if (!sound(addr, job.size) || !sound(addr + (job.count - 1) * job.size, job.size)) {
                out(ANSWER, seq, 2, 0, 0, d[6]);
                break;
            }
        } else {
            job.size = 1;
            job.count = (u32)d[6] << 8 | d[7];
            if (!job.count || !sound(addr, 1) || !sound(addr + job.count - 1, 1)) {
                out(ANSWER, seq, 2, 0, d[6], d[7]);
                break;
            }
        }
        job.kind = d[1];
        break;
    case 0x02:
        arg = addr;
        out(ANSWER, seq, 0, arg, 0, 0);
        break;
    case 0x03:
        if (!sound(addr, d[6]) || guarded(addr, d[6])) {
            out(ANSWER, seq, 2, 0, 0, d[6]);
            break;
        }
        if (d[6] == 1)
            REG8(addr) = (u8)arg;
        else if (d[6] == 2)
            REG16(addr) = (u16)arg;
        else
            REG32(addr) = arg;
        out(ANSWER, seq, 0, arg, 0, d[6]);
        break;
    case 0x05: {
        u32 v = 0;
        u8 status = 0;
        switch (d[2]) {
        case 0: v = VERSION; break;
        case 1: v = ticks; break;
        case 2: v = sr_get(); break;
        case 3: v = vbr_get(); break;
        case 5: v = lost; break;
        case 6: v = 2; break;
        case 9: v = given_up; break;
        case 10: v = side_stock_tick; break;
        case 11: v = longest; break;
        default: status = 3;
        }
        out(ANSWER, seq, status, v, 0, d[2]);
        break;
    }
    case 0x10:
        if (be32(d + 2) != 0x4f464621u) {
            out(ANSWER, seq, 2, 0, 0, 0);
            break;
        }
        out(ANSWER, seq, 0, 0, 0, 0);
        unhook();
        break;
    default:
        out(ANSWER, seq, 3, 0, 0, d[1]);
    }
}

/* A step of the job under way: a READ's next answer, or a BLOCK's
 * next five bytes, and its CRC at the end. */
static void work(void)
{
    int k;
    if (!job.kind || room() < 2)
        return;
    if (job.kind == 0x01) {
        out(ANSWER, job.seq, 0, peek(job.addr + job.done * job.size, job.size), (u8)job.done, job.size);
        if (++job.done == job.count)
            job.kind = 0;
        return;
    }
    if (job.done < job.count) {
        u32 at = job.done, i;
        u8 d[8] = { (u8)(at >> 16), (u8)(at >> 8), (u8)at, 0, 0, 0, 0, 0 };
        for (i = 0; i < 5 && job.done < job.count; i++, job.done++) {
            u32 v = REG8(job.addr + job.done);
            d[3 + i] = (u8)v;
            job.crc ^= v;
            for (k = 0; k < 8; k++)
                job.crc = (job.crc >> 1) ^ (0xedb88320u & (0u - (job.crc & 1)));
        }
        put(BYTES, d);
        return;
    }
    out(ANSWER, job.seq, 0, ~job.crc, (u8)(job.count >> 8), (u8)job.count);
    job.kind = 0;
}

static void receive(void)
{
    u8 d[8];
    u16 id;
    int i;
    if (REG16(UMSR0) & RX_BIT) {
        REG16(UMSR0) = RX_BIT;
        lost++;
    }
    if (!(REG16(RXPR0) & RX_BIT))
        return;
    id = (u16)(REG16(CAN_MB(RX_MB)) >> 2 & 0x7ff);
    for (i = 0; i < 8; i++)
        d[i] = REG8(CAN_MB(RX_MB) + 0x08 + i);
    REG16(RXPR0) = RX_BIT;
    if (id == REQUEST)
        request(d);
}

/* Every millisecond, ahead of the stock software's tick handler, at
 * that handler's interrupt level. */
void side_tick(void)
{
    u32 from;
    if (off)
        return;
    if (canary != MARK || _res_start[0] != MARK) {
        /* Somebody else writes here: out of the way, for good. */
        unhook();
        return;
    }
    from = REG32(TCNT0);
    ticks++;
    receive();
    work();
    if (ticks % 2000 == 0)
        out(RECORD, 0xb2, 0, ticks, (u8)(requests >> 8), (u8)requests);
    pump();
    /* The tick's timer counts down. */
    from -= REG32(TCNT0);
    if (from < 0x10000 && from > longest)
        longest = from;
}

/* Where the stock software's interrupt entry looks its handlers up:
 * read out of that entry's own code (at VBR + 0x600), which gets the
 * table's address from a literal into r1 and then the handler with
 * mov.l @(r0, r1), r3.  0 if the code there is not of that kind. */
static u32 *table(void)
{
    u32 pc = vbr_get() + 0x600, lit = 0;
    int i;
    for (i = 0; i < 24; i++, pc += 2) {
        u16 op = REG16(pc);
        if ((op & 0xff00) == 0xd100)
            lit = ((pc + 4) & ~3u) + (op & 0xff) * 4;
        else if (op == 0x031e && lit)
            return (u32 *)REG32(lit);
    }
    return 0;
}

/* Whether a resident is at work: its entry is in the table. */
int side_live(void)
{
    u32 *t = table();
    return t && t[0x400 / 0x20] == (u32)side_entry && !off;
}

/* Called once by start.S, in the stock application's task: the two
 * mailboxes, and the entry into the table, with interrupts held off
 * for the few instructions that takes.  It hooks nothing if the table
 * or the handler in it are not where and what the stock software's
 * are known to be. */
int side_init(void)
{
    u32 *t = table();
    u32 sr, old;
    if (!t || ((u32)t >> 24) != 0x8e)
        return -1;
    old = t[0x400 / 0x20];
    /* The stock handler is code in the flash, in the OS module. */
    if ((old >> 24) != 0x80 || old == (u32)side_entry)
        return -2;
    sr = sr_get();
    sr_set(sr | 0xf0);
    /* No interrupt of these two mailboxes, whatever they hold. */
    REG16(MBIMR0) = REG16(MBIMR0) | TX_BIT | RX_BIT;
    REG16(CAN_MB(TX_MB) + 0x10) = 0x0000;          /* sends */
    REG16(CAN_MB(RX_MB) + 0x10) = 0x0700;          /* out of use while it changes */
    REG16(CAN_MB(RX_MB) + 0x00) = (u16)(0x7f0 << 2);
    REG16(CAN_MB(RX_MB) + 0x04) = (u16)(0x003 << 2); /* 0x7F0-0x7F3 */
    REG16(CAN_MB(RX_MB) + 0x10) = 0x0300;          /* receives */
    REG16(UMSR0) = RX_BIT;
    REG16(RXPR0) = RX_BIT;
    slot = &t[0x400 / 0x20];
    side_stock_tick = old;
    out(RECORD, 0xb3, VERSION, sr, 0, 0);
    *slot = (u32)side_entry;
    sr_set(sr);
    return 0;
}

/* For programs brought in later (not yet): a line of text out. */
void side_log(const char *text)
{
    (void)text;
}
