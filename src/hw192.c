/* The older unit (8P0 035 192): an SH7750R, a companion chip with the
 * serial ports and an interrupt controller on chip select 1, byte-wide
 * ports on chip select 4 and a Fujitsu Coral graphics controller on
 * chip select 6.
 *
 * Every register written here is one the stock update loader writes,
 * with the loader's value and in the loader's order; docs/hardware.md
 * says what is known about them.  The loader never sets the direction
 * of the ports it drives, so the boot ROM has done that before it
 * starts; a program in the loader's place inherits the same state. */
#include "rnse.h"

#define INTEVT 0xff000028

/* On-chip interrupt controller and timer unit. */
#define ICR 0xffd00000
#define IPRA 0xffd00004
#define IPRB 0xffd00008
#define IPRC 0xffd0000c
#define TOCR 0xffd80000
#define TSTR 0xffd80004
#define TCOR0 0xffd80008
#define TCNT0 0xffd8000c
#define TCR0 0xffd80010

/* On-chip DMA controller: SAR, DAR, DMATCR, CHCR for channels 0-3 from
 * +0x00 and for 4-7 from +0x50; DMAOR at +0x40. */
#define DMAC 0xffa00000

/* Companion chip, through the uncached window. */
#define COMPANION_INTC 0xa7b00000
#define UART5 0xa7a50000 /* debug serial port */

/* Ports. */
#define PORT0_DATA 0xb2300001 /* bit 0: watchdog strobe */
#define PORT8_DATA 0xb2800001
#define LATCHES 0xb2d00000

/* Coral: host interface, display controller, video memory. */
#define CORAL_HOST 0xb9fc0000
#define CORAL_DISP 0xb9fd0000
#define VRAM 0xb8000000
#define SURFACE 0x00110000 /* offset in video memory, as the loader's */
#define STRIDE 448

/* For the way back (wayback.c): RAM starts at 0x8c000000; the stock loader lives from 0x8dd00000 up. */
const u32 way_back_scratch = 0x8c400000;
/* The strobe is port 0; bits 3 and 2 of port A are the CAN transceiver's
 * mode lines (both high: awake), which are on test pads of the board, so
 * the hand-over shows its progress there. */
const u32 way_back_strobe = 0xb2300001;
const u32 way_back_marker = 0xb2a00001;

const int screen_width = 400, screen_height = 235;

static volatile u32 ticks;
volatile int watchdog_hold;

void interrupt(void)
{
    u32 code = REG32(INTEVT);
    if (code == 0x400) {
        /* Timer 0 underflow: every 10 ms. */
        REG16(TCR0) &= (u16)~0x0100;
        ticks += 10;
        /* The watchdog wants an edge on its strobe regularly. */
        if (!watchdog_hold)
            REG8(PORT0_DATA) ^= 1;
        display_tick();
    } else if (code == 0x340) {
        /* Level 5: the CAN controller. */
        can_interrupt();
    } else if (code == 0x7e0 || code == 0x6a0) {
        /* DMTE7, DMTE3: the end of a transfer to a card processor
         * (pcm_stream). */
        pcm_interrupt();
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
    /* The timer counts 146999 down to 0 in 10 ms: 14.7 a microsecond. */
    return t * 1000 + udiv((146999 - c) * 10, 147);
}

void console_putc(char c)
{
    /* Status bit 9: transmit FIFO full. */
    while (REG16(UART5 + 0x08) & 0x0200)
        ;
    REG16(UART5 + 0x06) = (u8)c;
}

void screen_palette(int index, u8 r, u8 g, u8 b)
{
    /* Six bits a colour, in the upper bits of each byte. */
    REG32(CORAL_DISP + 0x800 + 4 * (index & 255)) = (u32)(r & 0xfc) << 16 | (u32)(g & 0xfc) << 8 | (b & 0xfc);
}

void screen_pixel(int x, int y, u8 colour)
{
    if (x < 0 || y < 0 || x >= screen_width || y >= screen_height)
        return;
    /* The chip is little-endian and the bus is not: pixel N of the
     * surface is the byte at address N ^ 3. */
    REG8(VRAM + SURFACE + ((y * STRIDE + x) ^ 3)) = colour;
}

void screen_blit(int x, int y, int w, int h, const u8 *pixels, int pitch)
{
    int i, j;
    if (x < 0 || y < 0 || x + w > screen_width || y + h > screen_height)
        return;
    if ((x | w | pitch | (int)(u32)pixels) & 3) {
        for (j = 0; j < h; j++)
            for (i = 0; i < w; i++)
                screen_pixel(x + i, y + j, pixels[j * pitch + i]);
        return;
    }
    /* Four pixels a word: pixel N is the byte at N ^ 3, so the word
     * holds them in the reverse of their order in memory. */
    for (j = 0; j < h; j++) {
        volatile u32 *to = (volatile u32 *)(VRAM + SURFACE + (y + j) * STRIDE + x);
        const u32 *from = (const u32 *)(pixels + j * pitch);
        for (i = 0; i < w / 4; i++)
            to[i] = __builtin_bswap32(from[i]);
    }
}

/* The faceplate controller: serial channel 3 of the companion chip, and
 * its reset line on bit 0 of the latch at 0xb6000004 (low = reset).
 * The values and their order are the stock operating system's; the
 * loader does not use the faceplate. */
#define UART3 0xa7a30000
#define LATCH16 0xb6000004

/* The fold-down display (docs/hardware.md, "Ports" and "A/D converter
 * and the fold-down display").  Port 3, direction at +0 and data at +1:
 * bit 3 runs the motor towards open, bit 4 towards closed, bit 2 is
 * low only when the display is fully open.  The stock software sets
 * bit 3 or bit 4, never both, and clears them to stop.  Its position is
 * A/D input 3, of the converter that examples/suite reads. */
#define PORT3 0xb2600000
#define ADC 0xa7d00000

const int display_input = 3;

void display_port_init(void)
{
    /* Latches low before the lines become outputs, so that nothing
     * left in them starts the motor. */
    REG8(PORT3 + 1) &= (u8)~0x18;
    REG8(PORT3) |= 0x18;
}

void display_motor(int dir)
{
    u8 v = REG8(PORT3 + 1) & (u8)~0x18;
    if (dir < 0)
        v |= 0x08;
    else if (dir > 0)
        v |= 0x10;
    REG8(PORT3 + 1) = v;
}

int display_open_switch(void)
{
    return !(REG8(PORT3 + 1) & 0x04);
}

/* One conversion of an input: channel n converts the inputs of its
 * group (0-3 or 4-6) up to n into results A, B, ... */
int adc_convert(int input)
{
    int n;
    REG16(ADC + 8) = (u16)(0x2000 | input);
    for (n = 0; n < 20000; n++) {
        if (REG16(ADC + 8) & 0x8000) {
            int v = REG16(ADC + 2 * (input & 3)) >> 6;
            REG16(ADC + 8) = 0;
            return v;
        }
    }
    REG16(ADC + 8) = 0;
    return -1;
}

void panel_port_init(void)
{
    REG16(UART3 + 0x04) = 0;                           /* port off */
    REG32(LATCH16) &= ~1u;                             /* controller in reset */
    REG16(UART3 + 0x08) = REG16(UART3 + 0x08) & 0xffc7; /* error flags */
    REG16(UART3 + 0x00) = 0x0011;                      /* format */
    REG16(UART3 + 0x02) = 0x005f;                      /* bit rate */
    REG16(UART3 + 0x0c) = 0x8080;                      /* both FIFOs emptied */
    delay_ms(20);
    REG32(LATCH16) |= 1;
    REG16(UART3 + 0x04) = 0x0030; /* transmitter and receiver on */
}

int panel_getc(void)
{
    u16 status = REG16(UART3 + 0x08);
    if (status & 0x0038) /* receive errors */
        REG16(UART3 + 0x08) = status & 0xffc7;
    if (!(status & 0x0040))
        return -1;
    return REG16(UART3 + 0x0a) & 0xff;
}

void panel_putc(u8 c)
{
    while (REG16(UART3 + 0x08) & 0x0200)
        ;
    REG16(UART3 + 0x06) = c;
}

/* What the stock software sends once the controller has started, and
 * the last of the frames with which it fades the lighting in. */
const u8 panel_settings[] = {
    0x02, 10, 0x40, 0x1d, 0x4b, 0x42, 0x40, 0x40, 0x40, 0x01, 0xff, 0x05,
    0x03, 13, 0x3c, 0x0a, 0x02, 0x0a, 0x04, 0x0a, 0x04, 0x0a, 0x04, 0x0a, 0x04, 0x0a, 0x0a,
    0x0a, 5,  0x00, 0xff, 0x1c, 0xff, 0x1c,
    0x09, 4,  0x00, 0x00, 0x0f, 0x06,
    0x06, 12, 0x00, 0x00, 0x00, 0x00, 0x30, 0x70, 0x00, 0x00, 0x09, 0xb0, 0x00, 0x00,
    0x07, 4,  0x00, 0x00, 0x00, 0x03,
    0x06, 12, 0x0f, 0x8a, 0x07, 0x98, 0x09, 0x98, 0x00, 0x00, 0x02, 0x18, 0x26, 0xd8,
    0, 0
};

/* The CD/DVD drive: ATA registers at odd addresses two apart, the data
 * register at a second address (where the DMA controller takes disc
 * data from in the stock software, and where the stock operating
 * system reads short answers word by word), alternate status and
 * device control in a third block.  The first byte of each pair is the
 * upper half of a data word.  The stock loader moves disc data by DMA
 * and waits for the drive's interrupt; this code polls. */
#define ATA_REG(n) (0xa7800001 + 2 * (n))
#define ATA_DATA 0xa7880000
#define ATA_CONTROL 0xa790000d

void cd_port_init(void)
{
    REG8(ATA_CONTROL) = 0x08; /* as the loader: no reset, interrupt line driven */
}

u8 cd_reg_read(int reg)
{
    return REG8(ATA_REG(reg));
}

void cd_reg_write(int reg, u8 v)
{
    REG8(ATA_REG(reg)) = v;
}

u8 cd_alt_status(void)
{
    return REG8(ATA_CONTROL);
}

u16 cd_data_read(void)
{
    return REG16(ATA_DATA);
}

void cd_data_write(u16 w)
{
    REG16(ATA_DATA) = w;
}

/* The CAN controller (an HCAN2): 16-bit registers, thirty-two
 * mailboxes of 0x20 bytes from +0x100 (identifier << 4 at +0, control
 * with the length at +4, data from +8, acceptance mask at +0x10).  The
 * set-up is the stock loader's: it does not reset the controller, which
 * the boot ROM has left running, and its bit timing (21 quanta a bit,
 * prescaler 14) is 100 kbit/s. */
#define CAN 0xa7c00000
#define CAN_MB(n) (CAN + 0x100 + 0x20 * (n))

void can_init(void)
{
    int n, i;
    REG16(CAN + 0x00) = 0x0000; /* run */
    for (n = 0; n < 32; n++) {
        for (i = 0; i < 0x14; i++)
            REG8(CAN_MB(n) + i) = 0;
        REG16(CAN_MB(n) + 0x04) = 0x0700; /* mailbox not in use */
    }
    REG16(CAN_MB(0) + 0x04) = 0x2300; /* receives, new over unread */
    REG16(CAN_MB(0) + 0x10) = 0x7ff0; /* whatever the identifier */
    REG16(CAN + 0x06) = 0x000d;       /* bit timing */
    REG16(CAN + 0x04) = 0xf300;
    REG16(CAN + 0x08) = 0x0001; /* reset flag taken back */
    REG16(CAN + 0x90) = 0;
    REG16(CAN + 0x92) = 0;
    REG16(CAN + 0x94) = 0;
    /* Interrupts: the loader's mask is 0xfebd, which lets the end of a
     * transmission through as well; this code waits for that itself and
     * takes the interrupt for received frames only, from mailbox 0, at
     * the level the loader gives the controller. */
    REG16(CAN + 0x0a) = 0xfffd;
    REG16(CAN + 0x50) = 0xffff;
    REG16(CAN + 0x52) = 0xfffe;
    REG16(CAN_MB(1) + 0x04) = 0x2000; /* sends */
    REG16(COMPANION_INTC + 0x12) = 0x0500;
    /* The transceiver awake: bits 3 and 2 of port A, set as the loader
     * and the installed software set them.  The installed software
     * lets the bus sleep when it is alone on it (after 4.7 s on a
     * bench), and nothing gets out of a sleeping transceiver. */
    REG8(0xb2a00001) |= 0x0c;
}

int can_send_for(u16 id, const u8 *data, int len, u32 us)
{
    u32 start = micros();
    int i;
    REG16(CAN_MB(1) + 0x00) = (u16)(id << 4);
    REG16(CAN_MB(1) + 0x02) = 0;
    for (i = 0; i < len && i < 8; i++)
        REG8(CAN_MB(1) + 0x08 + i) = data[i];
    REG16(CAN_MB(1) + 0x04) = (u16)(0x2000 | (len & 15));
    REG16(CAN + 0x22) = 0x0002; /* transmit request, mailbox 1 */
    while (!(REG16(CAN + 0x32) & 0x0002)) {
        if (micros() - start > us) {
            REG16(CAN + 0x2a) = 0x0002; /* cancel */
            /* The controller cancels when the attempt it is making is
             * over, a good millisecond later; a frame asked for before
             * that never goes out (on a unit every second frame of a
             * row was missing).  So: until the request is gone or the
             * cancellation is flagged, 20 ms at most. */
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

int can_take(u16 *id, u8 *data)
{
    int len, i;
    if (REG16(CAN + 0x5a) & 0x0001) {
        /* A frame arrived on top of an unread one. */
        REG16(CAN + 0x5a) = 0x0001;
        return -2;
    }
    if (!(REG16(CAN + 0x42) & 0x0001))
        return -1;
    *id = (u16)(REG16(CAN_MB(0)) >> 4 & 0x7ff);
    len = REG16(CAN_MB(0) + 0x04) & 15;
    if (len > 8)
        len = 8;
    for (i = 0; i < len; i++)
        data[i] = REG8(CAN_MB(0) + 0x08 + i);
    REG16(CAN + 0x42) = 0x0001; /* read: flag taken back */
    return len;
}

/* The 193's progress hook (rnse.h); the 192's bring-up does not call it. */
void (*board_init_step)(int step);

void board_init(void)
{
    int i;

    /* Interrupt controllers: nothing enabled.  Timers stopped. */
    REG16(IPRA) = 0;
    REG16(IPRB) = 0;
    REG16(IPRC) = 0;
    REG16(ICR) = 0;
    REG8(TOCR) = 0;
    REG8(TSTR) = 0;
    REG16(TCR0) = 0;
    for (i = 0; i <= 0x14; i += 2)
        REG16(COMPANION_INTC + i) = 0;
    REG16(COMPANION_INTC + 0x1e) = 0x0040;

    /* DMA: every channel off, then the controller's mode as the loader
     * has it.  The installed software leaves channels armed (the SD
     * card processors', the drive's) that would go on writing into
     * memory while this program runs. */
    for (i = 0; i < 8; i++)
        REG32(DMAC + 0x0c + (i < 4 ? i : i + 1) * 0x10) = 0;
    REG32(DMAC + 0x40) = 0x0000c001;

    /* Debug serial port: mode, bit rate, FIFO control, transmitter on. */
    REG16(UART5 + 0x00) = 0x0001;
    REG16(UART5 + 0x02) = 0x005f;
    REG16(UART5 + 0x0c) = 0x0000;
    REG16(UART5 + 0x04) = 0x0020;

    /* Output latches and port 8 as the loader sets them; which lines
     * these are is mostly unknown (docs/hardware.md, "Ports").
     *
     * The latch at 0xb6000004 and port 8 get bits added, not values:
     * the stock loader reads the latch and writes it back with bit 4
     * set, so everything else in it is as the installed software left
     * it.  Writing the bare value here, which is what the loader's
     * writes look like when it is started on its own in the emulator,
     * took the rest away on a real unit: the faceplate controller went
     * into reset (bit 0) and the screen dark, and the stock loader,
     * started afterwards for the way back, sat for 90 seconds without
     * reading the disc and gave up. */
    REG8(LATCHES + 0) = 0x1f;
    for (i = 2; i <= 6; i++)
        REG8(LATCHES + i) = 0x1f;
    REG8(PORT8_DATA) |= 0x20;
    REG8(PORT8_DATA) |= 0x40;
    REG32(0xb6000004) |= 0x00000010;

    /* Coral.  The first three writes go to its host interface (a key,
     * then two set-up bytes), with the watchdog strobe raised around
     * them as the loader has it. */
    REG32(CORAL_HOST + 0xfffc) = 0x41d7f053;
    REG8(CORAL_HOST + 0x07) = 0x02;
    REG8(CORAL_HOST + 0x27) = 0x1f;
    REG8(PORT0_DATA) = 0x01;
    REG32(0xb6000004) |= 0x00000010;
    REG8(CORAL_HOST + 0x2f) = 0x01;
    /* Display timing: 400 of 508 pixels a line, 235 of 263 lines. */
    REG16(CORAL_DISP + 0x02) = 0x1800;
    REG16(CORAL_DISP + 0x04) = 0x01fb;
    REG16(CORAL_DISP + 0x0a) = 0x018f;
    REG16(CORAL_DISP + 0x08) = 0x018f;
    REG16(CORAL_DISP + 0x0e) = 0x01b4;
    REG8(CORAL_DISP + 0x0d) = 0x24;
    REG8(CORAL_DISP + 0x0c) = 0x02;
    REG16(CORAL_DISP + 0x10) = 0x0106;
    REG16(CORAL_DISP + 0x14) = 0x00ea;
    REG16(CORAL_DISP + 0x16) = 0x00f2;
    /* Both palettes black. */
    for (i = 0x400; i < 0xc00; i += 4)
        REG32(CORAL_DISP + i) = 0;
    REG16(CORAL_DISP + 0x00) = 0x8000; /* display on, no layer yet */
    REG16(CORAL_DISP + 0x2e) = 0;
    REG16(CORAL_DISP + 0x2c) = 0;
    REG16(CORAL_DISP + 0x56) = 0;
    REG16(CORAL_DISP + 0x54) = 0;
    REG16(CORAL_DISP + 0x86) = 0;
    REG16(CORAL_DISP + 0x84) = 0;
    REG16(CORAL_DISP + 0xc0) = 0x8000;
    REG16(CORAL_DISP + 0xba) = 0x8000;
    REG8(CORAL_HOST + 0x27) = 0x13;
    REG8(PORT0_DATA) = 0x00;

    /* A black picture, then the layer that shows it: 8 bits per pixel,
     * 448 bytes a line, at SURFACE in video memory. */
    for (i = 0; i < STRIDE * 240; i += 4)
        REG32(VRAM + SURFACE + i) = 0;
    REG32(CORAL_DISP + 0x70) = 0x00070400;
    REG32(CORAL_DISP + 0x74) = SURFACE;
    REG32(CORAL_DISP + 0x78) = SURFACE;
    REG16(CORAL_DISP + 0x86) = 0;
    REG16(CORAL_DISP + 0x84) = 0;
    REG32(CORAL_DISP + 0x40) = 0x00000400;
    REG32(CORAL_DISP + 0x44) = 0;
    REG32(CORAL_DISP + 0x48) = 0;
    REG16(CORAL_DISP + 0x56) = 0;
    REG16(CORAL_DISP + 0x54) = 0;
    REG32(CORAL_DISP + 0x20) = 0x00000400;
    REG32(CORAL_DISP + 0x24) = 0;
    REG32(CORAL_DISP + 0x28) = 0;
    REG16(CORAL_DISP + 0x2e) = 0;
    REG16(CORAL_DISP + 0x2c) = 0;
    REG16(CORAL_DISP + 0x00) = 0x8008; /* that layer on */

    /* The tick: timer 0 at a quarter of the 58.8 MHz peripheral clock,
     * 147000 counts = 10 ms, interrupt level 7.  (The stock loader's
     * own tick, 73500 counts, is 5 ms.) */
    REG16(TCR0) = 0x0020;
    REG32(TCOR0) = 146999;
    REG32(TCNT0) = 146999;
    REG16(IPRA) = 0x7000;
    REG8(TSTR) = 1;

    {
        extern void irq_enable(void);
        irq_enable();
    }
}

/* The 193's SD host (hw193.c); the 192's cards go through sd.c. */
void (*sdhi_trace)(int cmd, u32 arg, u32 response, int result);
int sdhi_start(int slot) { (void)slot; return -1; }
int sdhi_read(u32 sector, void *buf) { (void)sector; (void)buf; return -1; }
