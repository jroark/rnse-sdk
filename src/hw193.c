/* The 2010 unit (8P0 035 193): an SH7770-family system-on-chip.
 *
 * No manual for the chip is public.  Every register written here is one
 * the stock update loader writes, with the loader's value and in the
 * loader's order; what the registers are is described, as far as it is
 * known, in docs/hardware-193.md.  Where the purpose of a write is not
 * known the comment says so: it is repeated because the real hardware
 * may need it, not because the emulator does. */
#include "rnse.h"

#define INTEVT 0xff000028

/* Interrupt controller. */
#define PINCTL 0xff802000 /* interrupt pins */
#define INT2PRI0 0xff804000
#define INT2MSKR 0xff804038   /* ones mask */
#define INT2MSKCR 0xff80403c  /* ones unmask */
#define INT2MSKR2 0xff804108
#define INT2MSKCR2 0xff80410c
#define MASK_TMU 0x00000002

/* Timer unit 0, channel 0. */
#define TSTR 0xffd80004
#define TCOR0 0xffd80008
#define TCNT0 0xffd8000c
#define TCR0 0xffd80010

/* Debug serial port. */
#define SCIF 0xffe40000
#define SCSMR (SCIF + 0x00)
#define SCSCR (SCIF + 0x08)
#define SCFTDR (SCIF + 0x0c)
#define SCFSR (SCIF + 0x10)
#define SCFCR (SCIF + 0x18)

/* Ports. */
#define PORT_A 0xffc40000
#define PORT_B 0xffc44000
#define WATCHDOG (PORT_B + 0x08) /* bit 0: strobe of the external watchdog */

/* Display unit. */
#define DU 0xfff80000
#define PLANE(n) (DU + 0x100 * (n))
#define PALETTE(n) (DU + 0x1000 * (n))
#define SURFACE 0x08110000  /* where the loader has its picture too */
#define STRIDE 832
/* The panel starts 16 columns and 8 lines into the surface with the
 * display timing below. */
#define LEFT 16
#define TOP 8

/* For the way back (wayback.c): RAM starts at 0x08000000; the stock loader lives from 0x09b60000 to below 0x0a000000, the picture is at 0x08110000. */
const u32 way_back_scratch = 0x0a800000;
/* The hand-over neither strobes nor shows its progress on this unit yet. */
const u32 way_back_strobe = 0;
const u32 way_back_marker = 0;

const int screen_width = 800, screen_height = 480;

/* The timers count a quarter of the peripheral clock: 12.462 MHz on a
 * bench 193 (249.3 million counts in 20.00 s on two channels, and a
 * clock of this kit's 3.8 s behind the world after 31 minutes at the
 * 12.4876 MHz taken before, which is the stock software's 12488 counts
 * for its millisecond; mon193, 2026-11-21).  A peripheral clock of
 * 49.85 MHz, then: one and a half times a crystal of 33.23 MHz, which
 * is what the one beside the chip, marked D332K, may well be.  Counts
 * in 10 ms: */
#define TICK_COUNTS 124620u

static volatile u32 ticks;
static u32 strobe;
volatile int watchdog_hold;

void interrupt(void)
{
    u32 code = REG32(INTEVT);
    if (code == 0x400) {
        /* Timer 0 underflow: every 10 ms.  The flag is read back after
         * it is cleared: the request takes a moment to go, and on a
         * bench 193 this handler, short as it is, was back at once for
         * the same underflow, every time: the kit's clock ran twice as
         * fast as the world's (40 s counted in 20, the timer itself
         * counting 12.46 MHz as it should; mon193, 2026-11-21). */
        REG16(TCR0) &= (u16)~0x0100;
        (void)REG16(TCR0);
        (void)REG16(TCR0);
        ticks += 10;
        /* The watchdog wants an edge on its strobe regularly; the stock
         * loader toggles it from this tick as well. */
        if (!watchdog_hold) {
            /* Bit 0 only, read-modify-write as the stock loader does:
             * the other pins of the port are not this code's. */
            strobe ^= 1;
            REG32(WATCHDOG) = (REG32(WATCHDOG) & ~1u) | strobe;
        }
        display_tick();
    } else if (code == 0x880) {
        can_interrupt();
    } else {
        /* A source nobody serves: returning would only bring it back
         * (a level-held one at once, for ever).  Treated as an
         * exception: shown, and the unit left to the watchdog. */
        unexpected(0, code, 0);
        for (;;)
            ;
    }
}

u32 millis(void)
{
    return ticks;
}

u32 micros(void)
{
    u32 t, c;
    do {
        t = ticks;
        c = REG32(TCNT0);
    } while (t != ticks);
    /* The timer counts TICK_COUNTS - 1 down to 0 in 10 ms. */
    return t * 1000 + udiv((TICK_COUNTS - 1 - c) * 100, TICK_COUNTS / 100);
}

void console_putc(char c)
{
    /* Wait for room in the transmit FIFO (TDFE), send, clear the flags. */
    while (!(REG16(SCFSR) & 0x0020))
        ;
    REG8(SCFTDR) = (u8)c;
    REG16(SCFSR) &= (u16)~0x0060;
}

/* The display unit's 8-bits-through-a-palette plane did not show on the
 * bench 193 (the picture came out a flat light grey, 2026-11-21), while
 * 16-bit planes showed their colours exactly (the loader's stripes).
 * So the 193 draws a 16-bit plane (RGB555, the display unit's format 2,
 * as the stock software's planes), and keeps a palette of its own: the
 * kit's drawing stays by colour index, each index an RGB555 word here. */
static u16 pal555[256];

void screen_palette(int index, u8 r, u8 g, u8 b)
{
    pal555[index & 255] = (u16)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

void screen_pixel(int x, int y, u8 colour)
{
    if (x < 0 || y < 0 || x >= screen_width || y >= screen_height)
        return;
    /* Through the uncached window, so that the display unit sees it. */
    REG16((SURFACE | 0xa0000000) + ((TOP + y) * STRIDE + LEFT + x) * 2) = pal555[colour];
}

/* A row (or rows) of colour-index pixels, each to its RGB555, faster
 * than a pixel at a time (the video, examples aaviewer).  Overrides the
 * weak one in common.c. */
void screen_blit(int x, int y, int w, int h, const u8 *pixels, int pitch)
{
    int i, j;
    for (j = 0; j < h; j++) {
        int yy = y + j;
        volatile u16 *out;
        if (yy < 0 || yy >= screen_height)
            continue;
        out = (volatile u16 *)((SURFACE | 0xa0000000) + ((TOP + yy) * STRIDE + LEFT + x) * 2);
        for (i = 0; i < w; i++) {
            int xx = x + i;
            if (xx >= 0 && xx < screen_width)
                out[i] = pal555[pixels[j * pitch + i]];
        }
    }
}

/* The faceplate controller: a serial port with FIFO at 0xff926000, and
 * its reset line on bit 1 of the port at 0xff909004 (low = reset).
 * The values and their order are the stock operating system's; the
 * loader does not use the faceplate.  (The operating system sets no
 * pin functions for this port: either the boot ROM has, or there are
 * none to set.) */
#define SCIF_FP 0xff926000
#define PORT_FP 0xff909000

/* The fold-down display (docs/hardware-193.md, "A/D converter and
 * ports").  The port at 0xff93f000 (direction +4, output +8, input
 * +0xc): output bit 18 opens, bit 19 closes, input bit 20 is low only
 * when the display is fully open.  Which bit does what is read off the
 * stock software's display key (V575 1050, 0x805faa22: a closed display
 * gets bit 18 and bit 19 cleared, an open one bit 19 and bit 18
 * cleared, either reverses a run the other way; 0x805fa742 stops at
 * position 94 with bit 20 low, or 894); a bench 193 had both lines as
 * outputs and off, bit 20 high and position 979 with its display closed
 * (examples/first193, 2026-11-20).  The position is input 4 of the
 * converter at 0xff93c000. */
#define MOTOR_PORT 0xff93f000
#define MOTOR_OPEN (1u << 18)
#define MOTOR_CLOSE (1u << 19)
#define ADC 0xff93c000

const int display_input = 4;
/* The display key opens and closes the display, as on the 192 (the
 * hardware rules: every program must let the disc out). */
int display_key_followed = 1;
const int cd_port_set_up_at_start = 1;

void display_port_init(void)
{
    REG32(MOTOR_PORT + 8) &= ~(MOTOR_OPEN | MOTOR_CLOSE);
    REG32(MOTOR_PORT + 4) |= MOTOR_OPEN | MOTOR_CLOSE;
}

void display_motor(int dir)
{
    u32 v = REG32(MOTOR_PORT + 8) & ~(MOTOR_OPEN | MOTOR_CLOSE);
    if (dir < 0)
        v |= MOTOR_OPEN;
    else if (dir > 0)
        v |= MOTOR_CLOSE;
    REG32(MOTOR_PORT + 8) = v;
}

int display_open_switch(void)
{
    return !(REG32(MOTOR_PORT + 0xc) & (1u << 20));
}

/* One conversion: channel n converts inputs 0..n, results at +2k. */
int adc_convert(int input)
{
    int n;
    REG16(ADC + 0x10) = (u16)(0x2000 | input);
    for (n = 0; n < 20000; n++) {
        if (REG16(ADC + 0x10) & 0x8000) {
            int v = REG16(ADC + 2 * input) >> 6;
            REG16(ADC + 0x10) = 0;
            return v;
        }
    }
    REG16(ADC + 0x10) = 0;
    return -1;
}

void panel_port_init(void)
{
    /* Only the reset line (bit 1 of +4) is this code's, as with the
     * stock software (0x8054fc7c in V5751050: bit 1 low, then high);
     * the port's other lines are left as they are.  The kit used to
     * write whole values here (0, 0xc0000000; 0, 1), copied from the
     * emulator, where the reads before them return 0: on the bench 193
     * the port reads 0xdfffffff/0xfffffffb, so that drove nearly all
     * its lines low, and the drive went on ejecting and loading
     * (2026-11-20). */
    REG16(SCIF_FP + 0x08) = 0;      /* port off */
    REG32(PORT_FP + 4) &= ~2u;      /* controller in reset */
    REG16(SCIF_FP + 0x18) = 0;
    REG16(SCIF_FP + 0x18) = 0x0006; /* both FIFOs held empty */
    REG16(SCIF_FP + 0x10) = REG16(SCIF_FP + 0x10) & 0x0060;
    REG16(SCIF_FP + 0x24) = 0;
    REG16(SCIF_FP + 0x00) = 0x0020; /* format: 8 bits, even parity */
    REG8(SCIF_FP + 0x04) = 0xa1;    /* bit rate */
    delay_ms(20);
    REG32(PORT_FP + 4) |= 2;
    REG16(SCIF_FP + 0x18) = 0;
    REG16(SCIF_FP + 0x08) = 0x0030; /* transmitter and receiver on */
}

int panel_getc(void)
{
    u16 status = REG16(SCIF_FP + 0x10);
    if (status & 0x0003) {
        int c = REG8(SCIF_FP + 0x14);
        /* Flags are cleared by writing zeroes to them after reading. */
        REG16(SCIF_FP + 0x10) = status & (u16)~0x0093;
        return c;
    }
    if (status & 0x0090) /* error, break */
        REG16(SCIF_FP + 0x10) = status & (u16)~0x0090;
    return -1;
}

void panel_putc(u8 c)
{
    /* Bit 5: room in the transmit FIFO. */
    while (!(REG16(SCIF_FP + 0x10) & 0x0020))
        ;
    REG8(SCIF_FP + 0x0c) = c;
    REG16(SCIF_FP + 0x10) = REG16(SCIF_FP + 0x10) & (u16)~0x0060;
}

/* What the stock software sends once the controller has started, and
 * the last of the frames with which it fades the lighting in. */
const u8 panel_settings[] = {
    0x02, 10, 0x40, 0x1d, 0x4b, 0x42, 0x40, 0x00, 0x40, 0x01, 0xff, 0x05,
    0x03, 13, 0x3c, 0x0a, 0x02, 0x0a, 0x04, 0x0a, 0x04, 0x0a, 0x04, 0x0a, 0x04, 0x0a, 0x0a,
    0x0a, 5,  0x00, 0xff, 0x1c, 0xff, 0x1c,
    0x09, 4,  0x00, 0x00, 0x0f, 0x06,
    0x06, 12, 0x00, 0x00, 0x00, 0x00, 0x30, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 4,  0x00, 0x00, 0x00, 0x03,
    0x06, 12, 0x0c, 0x80, 0x07, 0x98, 0x09, 0x98, 0x09, 0x0e, 0x02, 0x18, 0x26, 0xd8,
    0, 0
};

/* The CD/DVD drive on the chip's ATAPI interface: the ATA registers one
 * to a 32-bit word, alternate status and device control at +0x38, the
 * interface's own registers from +0x80.  Data words carry the first
 * byte of each pair in the lower half.  Set-up values and their order
 * are the stock loader's, which also works by programmed I/O; the one
 * thing left out is its enabling of the drive's interrupt (+0x88),
 * since this code polls. */
#define ATAPI 0xffc62100

void cd_port_init(void)
{
    REG32(ATAPI + 0xa4) |= 0x00000001;
    REG32(ATAPI + 0xc0) = 0x00140014; /* four timing registers */
    REG32(ATAPI + 0xc4) = 0x0c060c06;
    REG32(ATAPI + 0xc8) = 0x140c140c;
    REG32(ATAPI + 0xcc) = 0x06110611;
    REG32(ATAPI + 0x80) |= 0x00000060; /* interface control */
    REG32(ATAPI + 0xbc) = 0x00000001;
    REG32(ATAPI + 0x84) = 0;           /* status flags cleared */
    REG32(ATAPI + 0x18) = 0xa0;        /* drive 0 */
    while (REG32(ATAPI + 0x38) & 0x80)
        ;
    REG32(ATAPI + 0x38) = 0x08; /* device control: no reset */
    /* SET FEATURES: transfer mode, PIO 4. */
    REG32(ATAPI + 0x18) = 0xa0;
    REG32(ATAPI + 0x04) = 0x03;
    REG32(ATAPI + 0x08) = 0x0c;
    REG32(ATAPI + 0x1c) = 0xef;
    REG32(ATAPI + 0x38);
    while (REG32(ATAPI + 0x38) & 0x80)
        ;
    REG32(ATAPI + 0x1c); /* status read: takes the drive's interrupt back */
}

u8 cd_reg_read(int reg)
{
    return (u8)REG32(ATAPI + 4 * reg);
}

void cd_reg_write(int reg, u8 v)
{
    REG32(ATAPI + 4 * reg) = v;
}

u8 cd_alt_status(void)
{
    return (u8)REG32(ATAPI + 0x38);
}

u16 cd_data_read(void)
{
    u32 w = REG32(ATAPI);
    return (u16)((w & 0xff) << 8 | (w >> 8 & 0xff));
}

void cd_data_write(u16 w)
{
    REG32(ATAPI) = (u32)(w >> 8) | (u32)(w & 0xff) << 8;
}

/* The chip's CAN controller: the registers of the 192's, with another
 * mailbox layout (identifier << 2 at +0, acceptance mask at +4, data
 * from +8, control with the length at +0x10).  The set-up is the stock
 * loader's: reset, mailboxes, bit timing (21 quanta a bit, prescaler
 * 12: 100 kbit/s), run.  The loader receives in mailbox 24, so this
 * does too. */
#define CAN 0xffdf0000
#define CAN_MB(n) (CAN + 0x100 + 0x20 * (n))
#define CAN_RX 24

void can_init(void)
{
    int n;
    /* (The control register's top bit stays set, as the stock software
     * has it, 0x8000: with it cleared the controller has a mailbox's
     * identifier two bits further up, and a bench 193 set up by this
     * code with 1 and 0 here sent 0x7f8 as 0x1fe and took no frame for
     * what it was; with 0x8001 and 0x8000 it talked and listened as
     * before.  mon193's can_try, 2026-11-21.) */
    REG16(CAN + 0x00) = 0x8001; /* reset */
    REG16(CAN + 0x08) = 0x0001;
    /* Interrupts: the loader's mask is 0xfebd, which lets the end of a
     * transmission through as well; this code waits for that itself and
     * takes the interrupt for received frames only. */
    REG16(CAN + 0x0a) = 0xfffd;
    for (n = 0; n < 32; n++) {
        REG32(CAN_MB(n) + 0x00) = 0;
        REG32(CAN_MB(n) + 0x08) = 0;
        REG32(CAN_MB(n) + 0x0c) = 0;
        REG32(CAN_MB(n) + 0x10) = 0;
        REG16(CAN_MB(n) + 0x10) = 0x0700; /* mailbox not in use */
    }
    REG16(CAN_MB(1) + 0x10) = 0x0000;      /* sends */
    REG16(CAN_MB(CAN_RX) + 0x10) = 0x0300; /* receives */
    REG16(CAN_MB(CAN_RX) + 0x04) = 0x1ffc; /* whatever the identifier */
    REG16(CAN + 0x06) = 0x000b;            /* bit timing */
    REG16(CAN + 0x04) = 0xb730;
    REG16(CAN + 0x00) = 0x8000; /* run */
    REG16(CAN + 0x50) = (u16)~(1 << (CAN_RX - 16)); /* that mailbox's interrupt */
    REG16(CAN + 0x52) = 0xffff;
    /* Level and mask in the interrupt controller as the loader has them. */
    REG32(INT2PRI0 + 0x14) = 0x000a0000;
    REG32(INT2MSKCR) = 0x02000000;
}

/* The controller as the stock software left it, for a program it
 * started: no reset and the bit timing untouched (the bench 193's bus
 * stayed silent after can_init, and sending through the controller as
 * it was left worked: examples/sdload193).  Only mailbox 24 is set up
 * again, out of use while its identifier and mask change, to receive
 * whatever comes, and only its interrupt let through, as can_init has
 * it.  Mailbox 1 sends, as with can_init.  A controller without a bit
 * timing has not been set up by anybody: then can_init. */
void can_attach(void)
{
    /* Nobody set it up (a program started in the loader's place by the
     * boot ROM, or in the emulator): can_init's way, then. */
    if (!REG16(CAN + 0x04) && !REG16(CAN + 0x06)) {
        can_init();
        return;
    }
    REG16(CAN_MB(CAN_RX) + 0x10) = 0x0700; /* out of use */
    REG16(CAN_MB(CAN_RX) + 0x00) = 0;
    REG16(CAN_MB(CAN_RX) + 0x04) = 0x1ffc; /* whatever the identifier */
    REG16(CAN_MB(CAN_RX) + 0x10) = 0x0300; /* receives */
    REG16(CAN + 0x58) = (u16)(1 << (CAN_RX - 16)); /* nothing lost yet */
    REG16(CAN + 0x40) = (u16)(1 << (CAN_RX - 16)); /* nothing waiting */
    REG16(CAN + 0x50) = (u16)~(1 << (CAN_RX - 16));
    REG16(CAN + 0x52) = 0xffff;
    REG32(INT2PRI0 + 0x14) = 0x000a0000;
    REG32(INT2MSKCR) = 0x02000000;
}

int can_send_for(u16 id, const u8 *data, int len, u32 us)
{
    u32 start = micros();
    int i;
    REG16(CAN_MB(1) + 0x00) = (u16)(id << 2);
    for (i = 0; i < len && i < 8; i++)
        REG8(CAN_MB(1) + 0x08 + i) = data[i];
    REG16(CAN_MB(1) + 0x10) = (u16)(len & 15);
    REG32(CAN + 0x20) = 0x00000002; /* transmit request, mailbox 1 */
    while (!(REG16(CAN + 0x32) & 0x0002)) {
        if (micros() - start > us) {
            REG32(CAN + 0x28) = 0x00000002; /* cancel */
            /* As on the 192, where a unit showed it: the next frame
             * only once this one is really gone, 20 ms at most. */
            start = micros();
            while ((REG16(CAN + 0x22) & 0x0002) && !(REG16(CAN + 0x3a) & 0x0002) && micros() - start < 20000)
                ;
            REG16(CAN + 0x3a) = 0x0002;
            return -1;
        }
    }
    REG16(CAN + 0x32) = 0x0002; /* acknowledged: flag taken back */
    return 0;
}

u32 can_status(void)
{
    return (u32)REG16(CAN + 0x02) << 16 | REG16(CAN + 0x0c);
}

int can_send(u16 id, const u8 *data, int len)
{
    return can_send_for(id, data, len, 100000);
}

/* See rnse.h.  The waits are counted, since no timer may be running
 * yet.  What a bench unit showed (2026-11-22, a logic analyser on the
 * bus, nothing acknowledging): 400000 rounds of the first loop are 55 ms
 * of the frame sent over and over, and the copies of this the programs
 * had waited as long again for the cancel, 112 ms a frame with the
 * watchdog left alone.  That is the watchdog's limit, and the unit was
 * reset in the first frame or in the eighteenth.  So the strobe is fed
 * within the waits, a cancel gets the time of two frames, and once a
 * frame has gone unacknowledged the next ones wait 150000 rounds, some
 * 20 ms, until one is taken.  (Not fewer: the emulator runs a round in
 * a seventh of the time, and a frame takes its 1.2 ms on its bus too.) */
static int tell_unheard;

int can_tell(u16 id, const u8 *data, int len, void (*feed)(void))
{
    u32 n, rounds = tell_unheard ? 150000 : 400000;
    int i;
    REG16(CAN_MB(1) + 0x00) = (u16)(id << 2);
    for (i = 0; i < len && i < 8; i++)
        REG8(CAN_MB(1) + 0x08 + i) = data[i];
    REG16(CAN_MB(1) + 0x10) = (u16)(len & 15);
    REG32(CAN + 0x20) = 0x00000002; /* transmit request, mailbox 1 */
    for (n = 0; n < rounds && !(REG16(CAN + 0x32) & 0x0002); n++)
        if (feed && !(n & 0x1fff))
            feed();
    if (REG16(CAN + 0x32) & 0x0002) {
        REG16(CAN + 0x32) = 0x0002; /* acknowledged: flag taken back */
        tell_unheard = 0;
        return 0;
    }
    REG32(CAN + 0x28) = 0x00000002; /* cancel */
    for (n = 0; n < 20000 && (REG16(CAN + 0x22) & 0x0002) && !(REG16(CAN + 0x3a) & 0x0002); n++)
        if (feed && !(n & 0x1fff))
            feed();
    REG16(CAN + 0x3a) = 0x0002;
    tell_unheard = 1;
    return -1;
}

int can_take(u16 *id, u8 *data)
{
    int len, i;
    /* Mailboxes 31..16 have their flags in the first word of a pair. */
    if (REG16(CAN + 0x58) & (1 << (CAN_RX - 16))) {
        /* A frame arrived while one was unread, and was dropped. */
        REG16(CAN + 0x58) = (u16)(1 << (CAN_RX - 16));
        return -2;
    }
    if (!(REG16(CAN + 0x40) & (1 << (CAN_RX - 16))))
        return -1;
    *id = (u16)(REG16(CAN_MB(CAN_RX)) >> 2 & 0x7ff);
    len = REG16(CAN_MB(CAN_RX) + 0x10) & 15;
    if (len > 8)
        len = 8;
    for (i = 0; i < len; i++)
        data[i] = REG8(CAN_MB(CAN_RX) + 0x08 + i);
    REG16(CAN + 0x40) = (u16)(1 << (CAN_RX - 16)); /* read: flag taken back */
    return len;
}

void (*board_init_step)(int step);
#define STEP(n) do { if (board_init_step) board_init_step(n); } while (0)

/* Whether board_init sets the display unit up the kit's way (below);
 * 0 leaves it as the software before left it (sdload193 reads it so),
 * and screen_init then does it later. */
int board_init_display = 1;

static void screen_init_regs(void)
{
    int i;
    /* Display unit, stopped while it is set up: 832 x 496 display
     * period in a 1056 x 525 frame, data enable 800 wide. */
    REG32(DU + 0x00) = 0x00100000;
    REG32(DU + 0x04) = 0x07000000;
    REG32(DU + 0x0c) = 0x0000cb3f; /* clear every status bit */
    REG32(DU + 0x10) = 0;          /* no interrupts from it */
    REG32(DU + 0x14) = 0;
    REG32(DU + 0x18) = 0x76543210; /* no plane shown yet */
    for (i = 0x80; i <= 0x9c; i += 4)
        REG32(DU + i) = 0;
    REG32(0xfff90000) = 0x00100008;
    REG32(DU + 0x40) = 0x0b5;
    REG32(DU + 0x44) = 0x3f5;
    REG32(DU + 0x48) = 0x018;
    REG32(DU + 0x4c) = 0x208;
    REG32(DU + 0x50) = 0x41f;
    REG32(DU + 0x54) = 0x03f;
    REG32(DU + 0x58) = 0x20c;
    REG32(DU + 0x5c) = 0x20a;
    REG32(DU + 0x78) = 0x0d7;
    REG32(DU + 0x7c) = 0x320;
    REG32(DU + 0x00) = 0x00100100; /* running */
}

static void screen_init_plane(void)
{
    int i;
    STEP(9);
    /* A black picture before anything shows. */
    REG32(SURFACE | 0xa0000000) = 0;
    STEP(91);
    for (i = 0; i < STRIDE * 496 * 2; i += 4) {
        REG32((SURFACE | 0xa0000000) + i) = 0;
        if (board_init_step && (i & 0x7ffc) == 0x7ffc)
            board_init_step(100 + (i >> 15));
    }
    STEP(92);
    screen_palette(0, 0, 0, 0);
    STEP(93);
    for (i = 0; i < 256; i++)
        screen_palette(i, 0, 0, 0);

    /* Plane 6, 16 bits a pixel (RGB555, the display unit's format 2, as
     * the stock software's planes), the whole display period; the other
     * planes off.  Its stride is in pixels (STRIDE), so its surface is
     * STRIDE x 496 words.  Its transparent colour is a word this code
     * never writes (bit 15 is always 0 here), so the picture is whole. */
    for (i = 1; i <= 8; i++)
        REG32(PLANE(i)) = 0;
    REG32(PLANE(6) + 0x00) = 0x04000002;
    REG32(PLANE(6) + 0x04) = STRIDE;
    REG32(PLANE(6) + 0x08) = 0xff;
    /* As wide as the surface, as the loader's plane: the picture starts
     * LEFT pixels in, so a plane 800 wide would leave the panel's last
     * LEFT columns black. */
    REG32(PLANE(6) + 0x10) = STRIDE;
    REG32(PLANE(6) + 0x14) = 496;
    REG32(PLANE(6) + 0x18) = 0;
    REG32(PLANE(6) + 0x1c) = 0;
    REG32(PLANE(6) + 0x20) = SURFACE;
    REG32(PLANE(6) + 0x24) = SURFACE;
    REG32(PLANE(6) + 0x28) = SURFACE;
    REG32(PLANE(6) + 0x30) = 0;
    REG32(PLANE(6) + 0x34) = 0;
    REG32(PLANE(6) + 0x38) = 0x3ff0;
    REG32(PLANE(6) + 0x3c) = 0x0fff;
    REG32(PLANE(6) + 0x40) = 0;
    REG32(PLANE(6) + 0x44) = 0;
    REG32(PLANE(6) + 0x48) = 0x8000;
    REG32(PLANE(6) + 0x50) = 0x400;
    REG32(DU + 0x18) = 0x76d43210; /* plane 6 shown */
    REG32(DU + 0x14) = 0x000f0000;
}

void screen_init(void)
{
    screen_init_regs();
    screen_init_plane();
}

void board_init(void)
{
    int i;

    STEP(1);
    /* Interrupt pins and controller: priorities zero, everything
     * masked.  The first eight writes are the loader's; their meaning
     * is not known in detail. */
    REG32(PINCTL + 0x00) = 0x00600000;
    REG32(PINCTL + 0x1c) = 0x00e80000;
    REG32(PINCTL + 0x10) = 0x00000000;
    REG32(PINCTL + 0x44) = 0xff000000;
    REG32(PINCTL + 0x48) = 0x80000000;
    REG32(0xff803000) = 0xa5000000;
    REG32(PINCTL + 0x64) = 0x04000000;
    REG32(PINCTL + 0x68) = 0x80000000;
    REG32(0xff800020) = 0xff902200;
    for (i = 0; i < 12; i++)
        REG32(INT2PRI0 + 4 * i) = 0;
    REG32(INT2MSKR) = 0xffffffff;
    REG32(INT2MSKR2) = 0xffffffff;

    STEP(2);
    /* Timers stopped. */
    REG8(TSTR) = 0;
    REG16(TCR0) = 0;

    STEP(3);
    /* Three writes of the loader's to a block of unknown purpose. */
    REG32(0xfe00802c) = 0;
    REG32(0xfe00803c) = 0;
    REG16(0xfe008060) = 1;

    STEP(4);
    /* Debug serial port: FIFOs reset, format 8N1, the loader's clock
     * settings (+0x34 and +0x30: source and divisor of the bit clock),
     * transmitter on. */
    REG16(SCFCR) = 0x0006;
    REG16(SCSCR) = 0x0002;
    REG16(SCSMR) = 0x0000;
    REG16(SCIF + 0x34) = 0x4000;
    REG16(SCIF + 0x30) = 0x0036;
    REG16(SCIF + 0x20) = 0x0000;
    REG16(SCFCR) = 0x0000;
    REG16(SCSCR) = 0x0022;

    STEP(5);
    /* Graphics engine: reset and released; not used here. */
    REG32(0xffe80000) = 0x80000000;
    REG32(0xffe80000) = 0x00000000;
    REG32(0xffe80060) = 0x0000000e;

    STEP(6);
    /* Ports: bits 3 and 4 of port A are outputs and driven high (what
     * they switch is not known; the picture may depend on them). */
    /* Read-modify-write of only the bits the stock loader touches
     * (0x09e27680 in V5740002): port A bits 3-5 taken from their other
     * functions, 3 and 4 made outputs, 5 an input; port B bit 3 an
     * input.  Whole-register writes, as the kit made them before, would
     * have set every other pin of these ports as well. */
    REG32(PORT_A + 0x20) &= ~0x38u;
    REG32(PORT_A + 0x00) &= ~0x38u;
    REG32(PORT_A + 0x04) = (REG32(PORT_A + 0x04) | 0x18) & ~0x20u;
    REG32(PORT_B + 0x20) &= ~0x08u;
    REG32(PORT_B + 0x00) &= ~0x08u;
    REG32(PORT_B + 0x04) &= ~0x08u;
    REG32(PORT_A + 0x08) |= 0x18;
    strobe = 1;
    REG32(WATCHDOG) = (REG32(WATCHDOG) & ~1u) | strobe;

    STEP(7);
    if (board_init_display)
        screen_init_regs();

    STEP(8);
    /* A block the loader sets up next (0x09e389b4 and 0x09e389c6 in
     * V5740002), presumably the memory controller's: a key, then, with
     * interrupts masked (they are), +0x220 set from a field of +0x280
     * as the loader computes it, twice as it does.  The kit used to
     * write the constant 0xfffffffe there: what the computation gives
     * when +0x280 reads 0, as it does in the emulator.  On the bench
     * 193 that stopped the RAM within some 100 ms (probe193, 2026-11-20). */
    REG32(0xfe800100) = 0x0000a55a;
    REG32(0xfe800108) = 0x00000000;
    REG32(0xfe800110) = 0x00000004;
    for (i = 0; i < 2; i++) {
        REG32(0xfe800108) = 0x00010000;
        REG32(0xfe800220) = ((((REG32(0xfe800280) & 0x7fc00) >> 10) & 0x1ff) - 2) | 0x1000;
        REG32(0xfe800108) = 0x00000000;
    }

    if (board_init_display)
        screen_init_plane();

    STEP(10);
    /* The tick: timer 0 at a quarter of the 50 MHz peripheral clock
     * (TICK_COUNTS, above), interrupt level 7. */
    REG16(TCR0) = 0x0020;
    REG32(TCOR0) = TICK_COUNTS - 1;
    REG32(TCNT0) = TICK_COUNTS - 1;
    REG32(INT2PRI0) = 0x000e0000;
    REG32(INT2MSKCR) = MASK_TMU;
    REG8(TSTR) = 1;

    {
        extern void irq_enable(void);
        irq_enable();
    }
}

/* ---- The SD host interface of the chip, for the two card slots (and
 * the SD bridge in one of them): 16-bit registers in the layout of
 * Toshiba's and Renesas' SD hosts at 0xff90a000.  The steps and values
 * are those of the stock driver (0x8067b9ae.. in V5751050, logged on
 * the emulator, rnse docs/hardware-193.md "SD cards"), with two
 * differences: it is polled, not driven by interrupts, and the bus
 * stays one bit wide (the stock driver takes four with ACMD6; the SD
 * bridge answers on one line only).  Sectors come through the data
 * port by the program, not by DMA. */
#define SDHI 0xff90a000
#define SD_CMD (SDHI + 0x00)
#define SD_SELECT (SDHI + 0x02)
#define SD_ARG0 (SDHI + 0x04)
#define SD_ARG1 (SDHI + 0x06)
#define SD_RESP0 (SDHI + 0x0c)
#define SD_RESP1 (SDHI + 0x0e)
#define SD_INFO1 (SDHI + 0x1c)
#define SD_INFO2 (SDHI + 0x1e)
#define SD_MASK1 (SDHI + 0x20)
#define SD_MASK2 (SDHI + 0x22)
#define SD_CLK (SDHI + 0x24)
#define SD_SIZE (SDHI + 0x26)
#define SD_OPTION (SDHI + 0x28)
#define SD_DATA (SDHI + 0x30)
#define SD_DMA (SDHI + 0xd8)
#define SD_RESET (SDHI + 0xe0)

#define INFO1_RESPONSE 0x0001
#define INFO1_ACCESS 0x0004
#define INFO2_ERRORS 0x807f /* CRC, end bit, timeouts, illegal access */
#define INFO2_TIMEOUT 0x0040
#define INFO2_READ 0x0100   /* a block can be read from the data port */
#define INFO2_WRITE 0x0200  /* a block can be written to the data port */
#define INFO2_BUSY 0x4000 /* the command line is in use */

void (*sdhi_trace)(int cmd, u32 arg, u32 response, int result);
static int sdhi_ccs, sdhi_slot = -1;
static u32 sdhi_rca;
static u16 sdhi_clock;
/* The clock's divider once a card has started (bits 7..0 of +0x24: 0
 * half the interface's clock, 1 a quarter, 2 an eighth, ...); the stock
 * software takes 0, and so does the kit since examples/speed193 read the
 * SD bridge at both on the bench 193 without a bad sector (CMD18: 2.9
 * MB/s at 0, 1.48 at 1).  Takes effect at the next sdhi_start. */
u16 sdhi_fast_clock = 0x0000;

/* The program's idle work (sd_idle: the faceplate answered) while the
 * SD host waits, at most every millisecond and never from inside itself.
 * Without it a card's start (a slot switch of 50 ms, its readiness polled
 * in steps of 20 ms, each command's answer waited for up to 500 ms) left
 * the faceplate unanswered, and a bench 193 whose program started its
 * cards again and again (the viewer without a bridge, the menu loader
 * looking at its slots) went dark, faceplate, screen and keys, for good
 * (2026-11-21). */
static void sdhi_idle(void)
{
    static int inside;
    static u32 last;
    u32 now = millis();
    if (!sd_idle || inside || now == last)
        return;
    last = now;
    inside = 1;
    sd_idle();
    inside = 0;
}

static void sdhi_delay(u32 ms)
{
    u32 start = millis();
    while (millis() - start < ms)
        sdhi_idle();
}

static int sdhi_wait(u32 reg, u16 bits, u16 errors, u32 ms)
{
    u32 start = millis();
    for (;;) {
        u16 v = REG16(reg);
        if (REG16(SD_INFO2) & errors)
            return -(int)(REG16(SD_INFO2) & errors);
        if (v & bits)
            return 0;
        if (millis() - start > ms)
            return -0x10000;
        sdhi_idle();
    }
}

/* One command: the clock started, argument, status cleared, the
 * command's word (its index; 0x40 more for an application command:
 * the host then knows the response).  0, or minus the error bits. */
static int sdhi_command(u16 word, u32 arg, u32 *response)
{
    int r;
    u32 start = millis();
    /* Until the command line is free (CBSY, bit 14 of INFO2).  The kit
     * used to wait for bit 13 instead, which the emulator always shows
     * set; on these hosts it says the clock divider may be changed, and
     * with the clock running it waited out its 100 ms before every
     * command: some 4 KB/s on the bench 193. */
    while (REG16(SD_INFO2) & INFO2_BUSY)
        if (millis() - start > 10)
            break;
    REG16(SD_CLK) = sdhi_clock;
    REG16(SD_CLK) = (u16)(sdhi_clock | 0x0100);
    REG16(SD_ARG0) = (u16)arg;
    REG16(SD_ARG1) = (u16)(arg >> 16);
    REG16(SD_INFO1) = (u16)~(INFO1_RESPONSE | INFO1_ACCESS);
    REG16(SD_INFO2) = (u16)~INFO2_ERRORS;
    REG16(SD_CMD) = word;
    r = sdhi_wait(SD_INFO1, INFO1_RESPONSE, INFO2_ERRORS, 500);
    REG16(SD_INFO1) = (u16)~INFO1_RESPONSE;
    if (response)
        *response = (u32)REG16(SD_RESP0) | (u32)REG16(SD_RESP1) << 16;
    if (sdhi_trace)
        sdhi_trace(word, arg, response ? *response : 0, r);
    return r;
}

/* What the stock software does just before it starts its SD driver
 * (0x806530e0 in V5751050), with the same values: a pin function
 * register that takes the complement of its value first (0xff900800,
 * then 0xff900804: bit 24 cleared, 25 and 28 set), a key, and four
 * registers of the bus and pin blocks.  On the bench 193, locked by its
 * PIN, the stock software never got this far, and neither it nor the kit
 * reached a card or the SD bridge (2026-11-20).  Once is enough; each
 * register is told to sdhi_trace (command word 0x100 + n) as it was
 * before and after. */
static int sdhi_pins_done;

static void sdhi_pins(void)
{
    static const u32 regs[] = { 0xff900804, 0xff800018, 0xff80001c, 0xff800020, 0xff900840 };
    u32 v;
    int i;
    if (sdhi_pins_done)
        return;
    sdhi_pins_done = 1;
    for (i = 0; i < 5; i++)
        if (sdhi_trace)
            sdhi_trace(0x100 + i, regs[i], REG32(regs[i]), 0);
    v = (REG32(0xff900804) & 0xfeffffffu) | 0x12000000u;
    REG32(0xff900800) = ~v;
    REG32(0xff900804) = v;
    REG32(0xff900860) = 0xa55af00fu;
    REG32(0xff800018) = 0x0ff0f00fu;
    REG32(0xff80001c) = 0x01010101u;
    REG32(0xff900840) = 0x01010101u;
    REG32(0xff800020) = 0x00002200u;
    for (i = 0; i < 5; i++)
        if (sdhi_trace)
            sdhi_trace(0x110 + i, regs[i], REG32(regs[i]), 0);
}

int sdhi_start(int slot)
{
    u32 r = 0, start;
    int v2, e;
    sdhi_slot = -1;
    sdhi_pins();
    REG16(SD_RESET) = 0;
    REG16(SD_RESET) = 7;
    REG16(SD_DMA) = 0;               /* the data port is the program's */
    REG16(SD_OPTION) = 0x80d9;       /* the stock timeouts; bit 15: one data line */
    REG16(SDHI + 0xf0) = 0x00da;
    REG16(SDHI + 0xfc) = 0x00fc;
    REG16(SD_MASK1) = 0xffff;        /* no interrupts: polled */
    REG16(SD_MASK2) = 0xffff;
    REG16(SDHI + 0xb2) = 0xffff;
    /* Select 0 is the unit's physical slot 2, 1 is slot 1 (examples/
     * slot193 on the bench, 2026-11-20).  The switch takes time: with
     * 10 ms the first commands still reached the slot selected before;
     * 20 ms was enough on the unit, 50 is given. */
    REG16(SD_SELECT) = (u16)(slot & 1);
    sdhi_clock = 0x0020;             /* slow while the card starts, as stock */
    REG16(SD_CLK) = 0;
    sdhi_delay(50);
    sdhi_command(0x0000, 0, 0);      /* CMD0 */
    e = sdhi_command(0x0008, 0x1aa, &r); /* CMD8: version 2 if it answers */
    v2 = e == 0 && (r & 0xfff) == 0x1aa;
    if (e && e != -INFO2_TIMEOUT)
        return -2;
    start = millis();
    for (;;) {
        if (sdhi_command(0x0037, 0, 0))                            /* CMD55 */
            return -3;
        if (sdhi_command(0x0069, v2 ? 0x40ff8000 : 0x00ff8000, &r)) /* ACMD41 */
            return -4;
        if (r & 0x80000000)
            break;
        if (millis() - start > 1500)
            return -5;
        sdhi_delay(20);
    }
    sdhi_ccs = v2 && (r & 0x40000000);
    if (sdhi_command(0x0002, 0, 0))  /* CMD2 */
        return -6;
    if (sdhi_command(0x0003, 0, &r)) /* CMD3: the card's address */
        return -7;
    r &= 0xffff0000;
    sdhi_rca = r;
    sdhi_clock = sdhi_fast_clock;    /* faster from here */
    if (sdhi_command(0x0007, r, 0))  /* CMD7: selected */
        return -8;
    if (!sdhi_ccs && sdhi_command(0x0010, 512, 0)) /* CMD16 */
        return -9;
    REG16(SD_SIZE) = 512;
    sdhi_slot = slot;
    return 0;
}

/* count sectors from sector on with one CMD18.  The host counts the
 * blocks itself (the count at +0x0a, enabled by bit 8 of +0x08) and then
 * ends the transfer with CMD12 on its own, as Toshiba's and Renesas' SD
 * hosts do; each block is taken from the data port as for CMD17.  0, or
 * minus (0x100 + the block it stopped at) when a block did not come, and
 * -2 when the card refused the command. */
int sdhi_read_multi(u32 sector, u32 count, void *buf)
{
    u8 *out = buf;
    u32 b;
    int i, r;
    if (sdhi_slot < 0)
        return -1;
    if (count == 0)
        return 0;
    REG16(SD_SIZE) = 512;
    REG16(SDHI + 0x08) = 0x0100;     /* block count on: CMD12 by the host */
    REG16(SDHI + 0x0a) = (u16)count;
    if (sdhi_command(0x0012, sdhi_ccs ? sector : sector << 9, 0)) { /* CMD18 */
        REG16(SDHI + 0x08) = 0;
        return -2;
    }
    for (b = 0; b < count; b++) {
        r = sdhi_wait(SD_INFO2, INFO2_READ, INFO2_ERRORS, 500);
        if (r) {
            REG16(SDHI + 0x08) = 0x0001; /* stop */
            REG16(SDHI + 0x08) = 0;
            return -(int)(0x100 + b);
        }
        REG16(SD_INFO2) = (u16)~INFO2_READ;
        for (i = 0; i < 256; i++) {
            u16 w = REG16(SD_DATA);
            out[2 * i] = (u8)(w >> 8);
            out[2 * i + 1] = (u8)w;
        }
        out += 512;
    }
    r = sdhi_wait(SD_INFO1, INFO1_ACCESS, INFO2_ERRORS, 500);
    REG16(SD_INFO1) = (u16)~INFO1_ACCESS;
    REG16(SDHI + 0x08) = 0;
    return r ? -4 : 0;
}

/* As sdhi_read_multi the other way: CMD25, the host counting the
 * blocks and ending with CMD12; each block goes to the data port when
 * INFO2 bit 9 asks for one.  Then CMD13 until the card is ready for data
 * again in its transfer state (it is busy while it programs).  Written
 * from the layout of Toshiba's and Renesas' SD hosts; on the emulator
 * only so far. */
int sdhi_write_multi(u32 sector, u32 count, const void *buf)
{
    const u8 *in = buf;
    u32 b, status = 0, start;
    int i, r;
    if (sdhi_slot < 0)
        return -1;
    if (count == 0)
        return 0;
    REG16(SD_SIZE) = 512;
    REG16(SDHI + 0x08) = 0x0100;     /* block count on: CMD12 by the host */
    REG16(SDHI + 0x0a) = (u16)count;
    if (sdhi_command(0x0019, sdhi_ccs ? sector : sector << 9, &status) || (status & 0xfdf90000)) { /* CMD25 */
        REG16(SDHI + 0x08) = 0;
        return -2;
    }
    for (b = 0; b < count; b++) {
        r = sdhi_wait(SD_INFO2, INFO2_WRITE, INFO2_ERRORS, 500);
        if (r) {
            REG16(SDHI + 0x08) = 0x0001; /* stop */
            REG16(SDHI + 0x08) = 0;
            return -(int)(0x100 + b);
        }
        REG16(SD_INFO2) = (u16)~INFO2_WRITE;
        for (i = 0; i < 256; i++)
            REG16(SD_DATA) = (u16)(in[2 * i] << 8 | in[2 * i + 1]);
        in += 512;
    }
    r = sdhi_wait(SD_INFO1, INFO1_ACCESS, INFO2_ERRORS, 1000);
    REG16(SD_INFO1) = (u16)~INFO1_ACCESS;
    REG16(SDHI + 0x08) = 0;
    if (r)
        return -4;
    start = millis();
    for (;;) {
        if (sdhi_command(0x000d, sdhi_rca, &status) == 0 /* CMD13 */
            && (status & 0x100) && ((status >> 9) & 15) == 4)
            return 0;
        if (millis() - start > 1000)
            return -5;
    }
}

int sdhi_read(u32 sector, void *buf)
{
    u8 *out = buf;
    int i, r;
    if (sdhi_slot < 0)
        return -1;
    REG16(SD_SIZE) = 512;
    if (sdhi_command(0x0011, sdhi_ccs ? sector : sector << 9, 0)) /* CMD17 */
        return -2;
    r = sdhi_wait(SD_INFO2, INFO2_READ, INFO2_ERRORS, 500);
    if (r)
        return -3;
    REG16(SD_INFO2) = (u16)~INFO2_READ;
    for (i = 0; i < 256; i++) {
        u16 w = REG16(SD_DATA);
        out[2 * i] = (u8)(w >> 8);
        out[2 * i + 1] = (u8)w;
    }
    r = sdhi_wait(SD_INFO1, INFO1_ACCESS, INFO2_ERRORS, 500);
    REG16(SD_INFO1) = (u16)~INFO1_ACCESS;
    return r ? -4 : 0;
}
