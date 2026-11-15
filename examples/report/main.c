/* A report from a real unit: what the hardware's own registers say
 * about it, and the boot ROM, which is on no update disc.
 *
 * Before it touches anything the program reads a list of registers, as
 * the installed software and the boot ROM before it have left them:
 * the processor's identification, its clock and bus settings, timers,
 * interrupt priorities, serial port settings.  They are shown on the
 * screen, page after page, as address and value, to be photographed;
 * the last pages have the state the program was started in and check
 * sums of the boot ROM.
 *
 * If something on the CAN bus acknowledges frames (an adapter that is
 * not in listen-only mode, at 100 kbit/s), all of that and the boot ROM
 * itself go out there as well, over and over:
 *
 *   7F1  'R', unit (0x92 or 0x93), 2, number of registers,
 *        length of the ROM (3 bytes), pass
 *   7F2  index, size, address (4 bytes)        one pair of these
 *   7F3  index, size, value (4 bytes)          for every register,
 *        and for what follows them under made-up addresses: the
 *        state at the start (1-7) and, on the 192, what its two card
 *        processors say (0x10-0x16 and 0x20-0x26, see dsp.h)
 *   7F4  0, 0, 0, 0, CRC-32 of the ROM (4 bytes)
 *   7F0  offset (3 bytes), 5 bytes of the ROM
 *
 * `rnse report log.txt --rom rom.bin` puts a log of those frames back
 * together.  Only registers are read that the processor's manual has
 * (192) or that the stock software itself uses (193), and nothing is
 * written anywhere but to the screen, the serial port and the bus.
 *
 * After ROUNDS rounds of the pages the program hands over to the stock
 * loader, like the probe.  EXTRA_CFLAGS: -DROUNDS=n, -DPAGE_SECONDS=n,
 * and -DHEXPAGES to show the ROM itself on the screen as well, 784
 * bytes a page with a check byte at the end of every line (the sum of
 * the line's bytes), for a unit with nothing on its bus. */
#include "rnse.h"
#include "dsp.h"

#ifndef ROUNDS
#define ROUNDS 6
#endif
#ifndef PAGE_SECONDS
#define PAGE_SECONDS 8
#endif

struct reg {
    u32 addr;
    u8 size;
};

/* SH7750R, by its manual, and the board's own ports. */
static const struct reg regs_192[] = {
    { 0xff000030, 4 }, { 0xff000040, 4 }, { 0xff000044, 4 }, /* PVR, CVR, PRR */
    { 0xff00001c, 4 }, { 0xff000010, 4 },                     /* CCR, MMUCR */
    { 0xffc00000, 2 }, { 0xffc00004, 1 }, { 0xffc0000c, 1 }, { 0xffc00010, 1 }, /* FRQCR, STBCR, WTCSR, STBCR2 */
    { 0xff800000, 4 }, { 0xff800004, 2 }, { 0xff800008, 4 }, { 0xff80000c, 4 }, /* BCR1, BCR2, WCR1, WCR2 */
    { 0xff800010, 4 }, { 0xff800014, 4 }, { 0xff800018, 2 }, { 0xff80001c, 2 }, /* WCR3, MCR, PCR, RTCSR */
    { 0xff800024, 2 }, { 0xff80002c, 4 }, { 0xff800030, 2 }, { 0xff800040, 4 }, /* RTCOR, PCTRA, PDTRA, PCTRB */
    { 0xff800044, 2 }, { 0xff800048, 2 }, { 0xff800050, 2 }, { 0xfe0a00f0, 4 }, /* PDTRB, GPIOIC, BCR3, BCR4 */
    { 0xffd80000, 1 }, { 0xffd80004, 1 },                                       /* TOCR, TSTR */
    { 0xffd80008, 4 }, { 0xffd80010, 2 }, { 0xffd80014, 4 }, { 0xffd8001c, 2 }, /* TCOR0, TCR0, TCOR1, TCR1 */
    { 0xffd80020, 4 }, { 0xffd80028, 2 },                                       /* TCOR2, TCR2 */
    { 0xfe100004, 1 }, { 0xfe100008, 4 }, { 0xfe100010, 2 }, { 0xfe100014, 4 }, { 0xfe10001c, 2 }, /* TSTR2, timers 3 and 4 */
    { 0xffd00000, 2 }, { 0xffd00004, 2 }, { 0xffd00008, 2 }, { 0xffd0000c, 2 }, { 0xffd00010, 2 }, /* ICR, IPRA-IPRD */
    { 0xfe080000, 4 }, { 0xfe080040, 4 },                                       /* INTPRI00, INTMSK00 */
    { 0xffa00040, 4 },                                                          /* DMAOR */
    { 0xffa0000c, 4 }, { 0xffa0001c, 4 }, { 0xffa0002c, 4 }, { 0xffa0003c, 4 }, /* CHCR0-3 */
    { 0xffa0005c, 4 }, { 0xffa0006c, 4 }, { 0xffa0007c, 4 }, { 0xffa0008c, 4 }, /* CHCR4-7 */
    { 0xffe00000, 1 }, { 0xffe00004, 1 }, { 0xffe00008, 1 },                    /* SCI: SCSMR1, SCBRR1, SCSCR1 */
    { 0xffe80000, 2 }, { 0xffe80004, 1 }, { 0xffe80008, 2 }, { 0xffe80018, 2 }, /* SCIF: SCSMR2, SCBRR2, SCSCR2, SCFCR2 */
    { 0xffc8003c, 1 }, { 0xffc80040, 1 },                                       /* RCR1, RCR2 */
    /* The ports on chip select 4: direction and data of each. */
    { 0xb2300000, 1 }, { 0xb2300001, 1 }, { 0xb2400000, 1 }, { 0xb2400001, 1 },
    { 0xb2500000, 1 }, { 0xb2500001, 1 }, { 0xb2600000, 1 }, { 0xb2600001, 1 },
    { 0xb2700000, 1 }, { 0xb2700001, 1 }, { 0xb2800000, 1 }, { 0xb2800001, 1 },
    { 0xb2900000, 1 }, { 0xb2900001, 1 }, { 0xb2a00000, 1 }, { 0xb2a00001, 1 },
    /* The companion's interrupt controller. */
    { 0xa7b00000, 2 }, { 0xa7b00012, 2 }, { 0xa7b00016, 2 }, { 0xa7b0001e, 2 },
};

/* The 193: only what its stock software reads or writes itself. */
static const struct reg regs_193[] = {
    { 0xff000030, 4 }, { 0xff000040, 4 }, { 0xff000044, 4 }, /* PVR, CVR, PRR */
    { 0xff00001c, 4 },                                        /* CCR */
    { 0xffd80004, 1 }, { 0xffd80008, 4 }, { 0xffd80010, 2 },  /* TSTR, TCOR0, TCR0 */
    { 0xffd80020, 4 }, { 0xffd80028, 2 },                     /* TCOR2, TCR2 */
    { 0xffd81004, 1 }, { 0xffd81008, 4 }, { 0xffd81010, 2 },  /* the CAN timer */
    { 0xff802000, 4 },                                        /* interrupt pins */
    { 0xff804000, 4 }, { 0xff804004, 4 }, { 0xff804008, 4 }, { 0xff80400c, 4 }, /* INT2PRI0-11 */
    { 0xff804010, 4 }, { 0xff804014, 4 }, { 0xff804018, 4 }, { 0xff80401c, 4 },
    { 0xff804020, 4 }, { 0xff804024, 4 }, { 0xff804028, 4 }, { 0xff80402c, 4 },
    { 0xff804038, 4 }, { 0xff804108, 4 },                     /* the two mask registers */
    { 0xffe40000, 2 }, { 0xffe40030, 2 }, { 0xffe40034, 2 },  /* debug port: mode, divisor, clock source */
    { 0xff926000, 2 }, { 0xff926004, 1 },                     /* faceplate port: mode, bit rate */
    { 0xffdf0004, 2 }, { 0xffdf0006, 2 },                     /* CAN bit timing */
    { 0xff909000, 4 }, { 0xff909004, 4 },                     /* port with the faceplate's reset */
    { 0xff93f004, 4 }, { 0xff93f008, 4 }, { 0xff93f00c, 4 },  /* port with the display motor */
    { 0xffc44008, 4 },                                        /* port with the watchdog strobe */
    { 0xff972000, 4 }, { 0xff984000, 4 },                     /* audio clock, sound interface */
};

#define MAX_REGS 96
static u32 values[MAX_REGS + 7 + 2 * DSP_VALUES];
static const struct reg *regs;
static int nregs;
/* How many values follow the registers, and the address each goes by. */
static int nextra = 7;

static u32 extra_addr(int k)
{
    if (k < 7)
        return (u32)k + 1;
    k -= 7;
    return 0x10u * (1 + (u32)(k / DSP_VALUES)) + (u32)(k % DSP_VALUES);
}

static u32 rom_size;
static u32 rom_crc;
static u32 block_crc[16];
#define ROM ((const u8 *)0xa0000000)

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

static char *put_hex(char *p, u32 v, int digits)
{
    while (digits--)
        *p++ = "0123456789ABCDEF"[v >> (4 * digits) & 15];
    return p;
}

static int s; /* pixels per dot of the 192's picture */

static void line(int row, const char *text, u8 colour, int scale)
{
    screen_text(4 * s, (2 + row * 8 * scale) * s, text, colour, scale * s);
}

static void title(const char *what, int page, int pages)
{
    char text[56], *p = text;
    const char *q = "RNSE REPORT 19";
    screen_fill(0, 0, screen_width, screen_height, 0);
    while (*q)
        *p++ = *q++;
    *p++ = screen_width == 400 ? '2' : '3';
    *p++ = ' ';
    while (*what)
        *p++ = *what++;
    *p++ = ' ';
    p = put_hex(p, (u32)page, 2);
    *p++ = '/';
    p = put_hex(p, (u32)pages, 2);
    *p = 0;
    line(0, text, 2, 2);
}

/* Twelve registers a page: address and value. */
#define PER_PAGE 12
static void register_page(int page, int pages)
{
    int i;
    title("REGISTERS", page + 1, pages);
    for (i = 0; i < PER_PAGE; i++) {
        int n = page * PER_PAGE + i;
        char text[20], *p = text;
        if (n >= nregs)
            break;
        p = put_hex(p, regs[n].addr, 8);
        *p++ = ' ';
        p = put_hex(p, values[n], 2 * regs[n].size);
        *p = 0;
        line(1 + i, text, 1, 2);
    }
}

static void pair(int row, const char *name, u32 v)
{
    char text[24], *p = text;
    while (*name)
        *p++ = *name++;
    *p++ = ' ';
    p = put_hex(p, v, 8);
    *p = 0;
    line(row, text, 1, 2);
}

static void state_page(int page, int pages)
{
    title("START", page, pages);
    pair(1, "SR  ", start_state[0]);
    pair(2, "VBR ", start_state[1]);
    pair(3, "PR  ", start_state[2]);
    pair(4, "SP  ", start_state[3]);
    pair(5, "R4  ", start_args[0]);
    pair(6, "R5  ", start_args[1]);
    pair(7, "R6  ", start_args[2]);
    pair(9, "ROM LENGTH", rom_size);
    pair(10, "ROM CRC   ", rom_crc);
}

/* What the 192's card processors answered (dsp.h). */
static void dsp_page(int n, int page, int pages)
{
    static const char *const names[DSP_VALUES] = { "RESULT", "PINS  ", "CLOCK ", "HOST  ", "TIME 1", "TIME 2", "LOOPS " };
    int i;
    title(n ? "CARD PROCESSOR 1" : "CARD PROCESSOR 0", page, pages);
    for (i = 0; i < DSP_VALUES; i++)
        pair(1 + i, names[i], dsp_values[n][i]);
}

static void crc_page(int page, int pages)
{
    int i;
    title("ROM BY 4K", page, pages);
    for (i = 0; i < 12 && (u32)i * 0x1000 < rom_size; i++) {
        char text[20], *p = text;
        p = put_hex(p, (u32)i * 0x1000, 5);
        *p++ = ' ';
        p = put_hex(p, block_crc[i], 8);
        *p = 0;
        line(1 + i, text, 1, 2);
    }
}

#ifdef HEXPAGES
/* 28 lines of 28 bytes: offset, the bytes, the sum of the bytes. */
#define HEX_PER_PAGE (28 * 28)
static void hex_page(u32 page, u32 pages)
{
    u32 row;
    title("ROM", (int)page + 1, (int)pages);
    for (row = 0; row < 28; row++) {
        u32 at = page * HEX_PER_PAGE + row * 28, i;
        char text[68], *p = text;
        u8 sum = 0;
        if (at >= rom_size)
            break;
        p = put_hex(p, at, 5);
        *p++ = ' ';
        for (i = 0; i < 28; i++) {
            u8 b = at + i < rom_size ? ROM[at + i] : 0xff;
            sum = (u8)(sum + b);
            p = put_hex(p, b, 2);
        }
        *p++ = ' ';
        p = put_hex(p, sum, 2);
        *p = 0;
        screen_text(2 * s, (10 + row * 8) * s, text, 1, s);
    }
}
#endif

/* The stream on the bus: one frame at a time, in an endless round. */
static int listener;
/* Nobody acknowledges (a logic analyser on the wires and no other
 * node): each frame is given 3 ms, in which it is on the bus at least
 * once, and the stream goes on regardless. */
static int blind;
static u32 stream, pass;

static int xmit(u16 id, const u8 *d)
{
    int r = can_send_for(id, d, 8, blind ? 3000 : 100000);
    return blind ? 0 : r;
}

static int send_next(void)
{
    u8 d[8];
    u32 at = stream;
    int i;
    if (at == 0) {
        d[0] = 'R';
        d[1] = screen_width == 400 ? 0x92 : 0x93;
        d[2] = 2;
        d[3] = (u8)nregs;
        d[4] = (u8)(rom_size >> 16);
        d[5] = (u8)(rom_size >> 8);
        d[6] = (u8)rom_size;
        d[7] = (u8)pass;
        if (xmit(0x7f1, d))
            return -1;
    } else if (at <= (u32)(2 * (nregs + nextra))) {
        /* The registers, then the values that have made-up addresses. */
        int n = (int)(at - 1) / 2;
        u32 v = (at - 1) & 1 ? values[n] : n < nregs ? regs[n].addr : extra_addr(n - nregs);
        d[0] = (u8)n;
        d[1] = n < nregs ? regs[n].size : 4;
        for (i = 0; i < 4; i++)
            d[2 + i] = (u8)(v >> (24 - 8 * i));
        d[6] = d[7] = 0;
        if (xmit((at - 1) & 1 ? 0x7f3 : 0x7f2, d))
            return -1;
    } else if (at == (u32)(2 * (nregs + nextra)) + 1) {
        d[0] = d[1] = d[2] = d[3] = 0;
        for (i = 0; i < 4; i++)
            d[4 + i] = (u8)(rom_crc >> (24 - 8 * i));
        if (xmit(0x7f4, d))
            return -1;
    } else {
        u32 off = (at - (u32)(2 * (nregs + nextra)) - 2) * 5;
        d[0] = (u8)(off >> 16);
        d[1] = (u8)(off >> 8);
        d[2] = (u8)off;
        for (i = 0; i < 5; i++)
            d[3 + i] = off + (u32)i < rom_size ? ROM[off + (u32)i] : 0xff;
        if (xmit(0x7f0, d))
            return -1;
        if (off + 5 >= rom_size) {
            stream = 0;
            pass++;
            return 0;
        }
    }
    stream = at + 1;
    return 0;
}

/* Show a page for a while; meanwhile the stream goes on, with
 * acknowledgements if anybody gives them and without if not. */
static void hold(u32 seconds)
{
    u32 until = millis() + seconds * 1000;
    if (!listener && !blind) {
        listener = send_next() == 0;
        blind = !listener;
    }
    line(28, listener ? "CAN: SENDING" : "CAN: SENDING, NOBODY ANSWERS", 3, 1);
    while ((s32)(millis() - until) < 0) {
        if (send_next()) {
            listener = 0;
            blind = 1;
        }
    }
}

int main(void)
{
    int i, round, pages, page;
    u32 limit;

    /* First of all, and before anything is set up: the registers. */
    if (screen_width == 400) {
        regs = regs_192;
        nregs = sizeof regs_192 / sizeof regs_192[0];
    } else {
        regs = regs_193;
        nregs = sizeof regs_193 / sizeof regs_193[0];
    }
    if (nregs > MAX_REGS)
        nregs = MAX_REGS;
    for (i = 0; i < nregs; i++) {
        switch (regs[i].size) {
        case 1: values[i] = REG8(regs[i].addr); break;
        case 2: values[i] = REG16(regs[i].addr); break;
        default: values[i] = REG32(regs[i].addr); break;
        }
    }
    for (i = 0; i < 4; i++)
        values[nregs + i] = start_state[i];
    for (i = 0; i < 3; i++)
        values[nregs + 4 + i] = start_args[i];

    board_init();
    console_puts("\nrnse sdk: report\n");
    s = screen_width / 400;
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 96, 0);
    screen_palette(3, 120, 120, 120);

    if (screen_width == 400) {
        int n;
        title("CARD PROCESSORS", 0, 0);
        dsp_probe();
        for (n = 0; n < 2; n++) {
            for (i = 0; i < DSP_VALUES; i++) {
                values[nregs + 7 + n * DSP_VALUES + i] = dsp_values[n][i];
                console_hex(dsp_values[n][i]);
                console_puts(i == DSP_VALUES - 1 ? "\n" : " ");
            }
        }
        nextra = 7 + 2 * DSP_VALUES;
    }

    /* The boot ROM: the 192's is the 48 KiB below the first module.
     * The 193's is somewhere in the 4 MiB its loader never erases:
     * up to the last byte there that is not 0xff, at most 1 MiB. */
    if (screen_width == 400) {
        rom_size = 0xc000;
    } else {
        limit = 0x100000;
        while (limit > 0x1000 && ROM[limit - 1] == 0xff)
            limit--;
        rom_size = (limit + 0xfff) & ~0xfffu;
    }
    title("CHECKING THE ROM", 0, 0);
    rom_crc = crc32(ROM, rom_size);
    for (i = 0; i < 16 && (u32)i * 0x1000 < rom_size; i++)
        block_crc[i] = crc32(ROM + i * 0x1000, 0x1000);

    for (i = 0; i < nregs; i++) {
        console_hex(regs[i].addr);
        console_puts(" ");
        console_hex(values[i]);
        console_puts("\n");
    }
    console_puts("rom ");
    console_hex(rom_size);
    console_puts(" crc ");
    console_hex(rom_crc);
    console_puts("\n");

    can_init();
    pages = (nregs + PER_PAGE - 1) / PER_PAGE + 2 + (nextra > 7 ? 2 : 0);
    for (round = 0; round < ROUNDS; round++) {
        for (page = 0; page < (nregs + PER_PAGE - 1) / PER_PAGE; page++) {
            register_page(page, pages);
            hold(PAGE_SECONDS);
        }
        if (nextra > 7) {
            dsp_page(0, pages - 3, pages);
            hold(PAGE_SECONDS);
            dsp_page(1, pages - 2, pages);
            hold(PAGE_SECONDS);
        }
        state_page(pages - 1, pages);
        hold(PAGE_SECONDS);
        crc_page(pages, pages);
        hold(PAGE_SECONDS);
#ifdef HEXPAGES
        {
            u32 n = (rom_size + HEX_PER_PAGE - 1) / HEX_PER_PAGE, k;
            for (k = 0; k < n; k++) {
                hex_page(k, n);
                hold(PAGE_SECONDS);
            }
        }
#endif
    }

    title("DONE", 0, 0);
    if (!stock_loader_carried()) {
        line(2, "NO STOCK LOADER: NO WAY BACK", 2, 2);
        for (;;)
            cpu_idle();
    }
    line(2, "STARTING THE STOCK LOADER", 2, 2);
    console_puts("starting the stock loader\n");
    i = stock_update();
    line(4, "THE STOCK LOADER DID NOT UNPACK", 2, 2);
    console_puts("the stock loader did not unpack: ");
    console_hex((u32)i);
    console_puts("\n");
    for (;;)
        cpu_idle();
}
