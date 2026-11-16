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

static volatile u32 ticks;
static u32 strobe;
volatile int watchdog_hold;

void interrupt(void)
{
    u32 code = REG32(INTEVT);
    if (code == 0x400) {
        /* Timer 0 underflow: every 10 ms. */
        REG16(TCR0) &= (u16)~0x0100;
        ticks += 10;
        /* The watchdog wants an edge on its strobe regularly; the stock
         * loader toggles it from this tick as well. */
        if (!watchdog_hold) {
            strobe ^= 1;
            REG32(WATCHDOG) = strobe;
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
    /* The timer counts 124875 down to 0 in 10 ms: 12.5 a microsecond. */
    return t * 1000 + udiv((124875 - c) * 2, 25);
}

void console_putc(char c)
{
    /* Wait for room in the transmit FIFO (TDFE), send, clear the flags. */
    while (!(REG16(SCFSR) & 0x0020))
        ;
    REG8(SCFTDR) = (u8)c;
    REG16(SCFSR) &= (u16)~0x0060;
}

void screen_palette(int index, u8 r, u8 g, u8 b)
{
    /* The plane uses the second of the four palettes. */
    REG32(PALETTE(2) + 4 * (index & 255)) = (u32)r << 16 | (u32)g << 8 | b;
}

void screen_pixel(int x, int y, u8 colour)
{
    if (x < 0 || y < 0 || x >= screen_width || y >= screen_height)
        return;
    /* Through the uncached window, so that the display unit sees it. */
    REG8((SURFACE | 0xa0000000) + (TOP + y) * STRIDE + LEFT + x) = colour;
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
 * +0xc): output bits 19 and 18 drive the motor, input bit 20 is low
 * only when the display is fully open.  Which of the two bits closes is
 * assumed (19), as in the emulator's model: nothing here has been tried
 * on a real 193.  The position is input 4 of the converter at
 * 0xff93c000. */
#define MOTOR_PORT 0xff93f000
#define MOTOR_OPEN (1u << 18)
#define MOTOR_CLOSE (1u << 19)
#define ADC 0xff93c000

const int display_input = 4;

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
    REG16(SCIF_FP + 0x08) = 0;      /* port off */
    REG32(PORT_FP + 4) = 0;         /* controller in reset */
    REG32(PORT_FP + 0) = 0;
    REG32(PORT_FP + 0) = 0xc0000000;
    REG32(PORT_FP + 4) = 1;
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
    REG16(CAN + 0x00) = 0x0001; /* reset */
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
    REG16(CAN + 0x00) = 0x0000; /* run */
    REG16(CAN + 0x50) = (u16)~(1 << (CAN_RX - 16)); /* that mailbox's interrupt */
    REG16(CAN + 0x52) = 0xffff;
    /* Level and mask in the interrupt controller as the loader has them. */
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

int can_send(u16 id, const u8 *data, int len)
{
    return can_send_for(id, data, len, 100000);
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

void board_init(void)
{
    int i;

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

    /* Timers stopped. */
    REG8(TSTR) = 0;
    REG16(TCR0) = 0;

    /* Three writes of the loader's to a block of unknown purpose. */
    REG32(0xfe00802c) = 0;
    REG32(0xfe00803c) = 0;
    REG16(0xfe008060) = 1;

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

    /* Graphics engine: reset and released; not used here. */
    REG32(0xffe80000) = 0x80000000;
    REG32(0xffe80000) = 0x00000000;
    REG32(0xffe80060) = 0x0000000e;

    /* Ports: bits 3 and 4 of port A are outputs and driven high (what
     * they switch is not known; the picture may depend on them). */
    REG32(PORT_A + 0x20) = 0;
    REG32(PORT_A + 0x00) = 0;
    REG32(PORT_A + 0x04) = 0x18;
    REG32(PORT_B + 0x20) = 0;
    REG32(PORT_B + 0x00) = 0;
    REG32(PORT_B + 0x04) = 0;
    REG32(PORT_A + 0x08) = 0x18;
    REG32(WATCHDOG) = strobe = 1;

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

    /* A block the loader sets up next: a key, then three registers.
     * Purpose not known. */
    REG32(0xfe800100) = 0x0000a55a;
    REG32(0xfe800108) = 0x00000000;
    REG32(0xfe800110) = 0x00000004;
    REG32(0xfe800108) = 0x00010000;
    REG32(0xfe800220) = 0xfffffffe;
    REG32(0xfe800108) = 0x00000000;

    /* A black picture before anything shows. */
    for (i = 0; i < STRIDE * 496; i += 4)
        REG32((SURFACE | 0xa0000000) + i) = 0;
    for (i = 0; i < 256; i++)
        screen_palette(i, 0, 0, 0);

    /* Plane 6: 8 bits per pixel through palette 2, no transparent
     * colour, the whole display period; the other planes off. */
    for (i = 1; i <= 8; i++)
        REG32(PLANE(i)) = 0;
    REG32(PLANE(6) + 0x00) = 0x00024100;
    REG32(PLANE(6) + 0x04) = STRIDE;
    REG32(PLANE(6) + 0x08) = 0xff;
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
    REG32(PLANE(6) + 0x48) = 0;
    REG32(PLANE(6) + 0x50) = 0x400;
    REG32(DU + 0x18) = 0x76d43210; /* plane 6 shown */
    REG32(DU + 0x14) = 0x000f0000;

    /* The tick: timer 0 at a quarter of the 50 MHz peripheral clock
     * (49.95 MHz going by the stock software's 12488 counts for its
     * 1 ms), 124876 counts = 10 ms, interrupt level 7.  (The stock
     * loader's own tick, 62438 counts, is 5 ms.) */
    REG16(TCR0) = 0x0020;
    REG32(TCOR0) = 124875;
    REG32(TCNT0) = 124875;
    REG32(INT2PRI0) = 0x000e0000;
    REG32(INT2MSKCR) = MASK_TMU;
    REG8(TSTR) = 1;

    {
        extern void irq_enable(void);
        irq_enable();
    }
}
