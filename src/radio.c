/* The radio processor of the 192: a second computer in the unit (an
 * NEC V850 on a board of its own) that has the tuner, the amplifier
 * and the power.  The main processor talks to it on serial channel 1,
 * in frames: 0x01, a command, the length of the data in 16 bits, the
 * data, and the complement of the sum of all that.  It answers a
 * question with a frame of the same command and a setting with command
 * 0x20 and a zero; its frames start with 0x00.  It also speaks
 * unasked (its status, command 0x88, after a change).
 *
 * The line is used as the installed software leaves it (bit rate,
 * format): a program started by that software, or by the SD loader,
 * finds it so.  docs/hardware.md, "Radio/audio processor". */
#include "rnse.h"

#define REG16(a) (*(volatile u16 *)(a))
#define UART1 0xa7a10000

static int getc_for(u32 ms)
{
    u32 start = millis();
    for (;;) {
        u16 status = REG16(UART1 + 0x08);
        if (status & 0x0038) /* receive errors stop the receiver */
            REG16(UART1 + 0x08) = status & 0xffc7;
        if (status & 0x0040)
            return REG16(UART1 + 0x0a) & 0xff;
        if (millis() - start >= ms)
            return -1;
    }
}

/* One frame from the radio processor: its command, its data. */
static int frame(u8 *data, int max, int *n, u32 ms)
{
    int c, cmd, len, i;
    u8 sum;
    /* (Whatever is not a frame's start is the rest of one that was
     * not waited for.) */
    do {
        c = getc_for(ms);
        if (c < 0)
            return -1;
    } while (c != 0x00);
    cmd = getc_for(50);
    len = getc_for(50);
    c = getc_for(50);
    if (cmd < 0 || len < 0 || c < 0)
        return -1;
    len = len << 8 | c;
    sum = (u8)(cmd + (len >> 8) + len);
    if (len > 300)
        return -1;
    for (i = 0; i < len; i++) {
        c = getc_for(50);
        if (c < 0)
            return -1;
        sum = (u8)(sum + c);
        if (i < max)
            data[i] = (u8)c;
    }
    c = getc_for(50);
    if (c < 0 || (u8)c != (u8)~sum)
        return -1;
    *n = len < max ? len : max;
    return cmd;
}

/* The command number of the last answer: the command's own, or 0x20
 * for an acknowledgement. */
int radio_answered;

/* What it last refused a frame with (the acknowledgement's byte when
 * it is not zero), and how often a frame had to be sent again for
 * that.  A unit's radio processor refuses about half of the frames of
 * a start, and the first one after it, and takes them a moment later:
 * it is busy, not deaf.  (Seen on a unit: byte 0x07 for eight of the
 * nineteen frames of radio_start, 0x08 for one, each taken when sent
 * again 100 ms later.) */
int radio_refusal, radio_refusals;

/* How long an answer is waited for, and how often into silence. */
static u32 patience = 600;
static int tries = 3;

int radio_command(u8 cmd, const u8 *data, int n, u8 *answer, int max)
{
    int try, i, got, len = 0, silent = 0;
    if (screen_width != 400)
        return -1; /* the 193's radio is spoken to differently, and not from here */
    /* Three times into silence, and for two seconds against refusals. */
    for (try = 0; silent < tries && try < 20; try++) {
        u16 control = REG16(UART1 + 0x04);
        u8 sum = (u8)(0x01 + cmd + (n >> 8) + n);
        u32 start;
        /* The installed software has the transmitter on only while it
         * sends, and its interrupts are nobody's here. */
        REG16(UART1 + 0x08) = REG16(UART1 + 0x08) & 0xffc7;
        while (getc_for(0) >= 0)
            ;
        REG16(UART1 + 0x04) = (u16)((control | 0x0030) & ~0x00c4);
        for (i = -4; i <= n; i++) {
            u8 b = i == -4 ? 0x01 : i == -3 ? cmd : i == -2 ? (u8)(n >> 8) : i == -1 ? (u8)n : i < n ? data[i] : (u8)~sum;
            if (i >= 0 && i < n)
                sum = (u8)(sum + b);
            start = millis();
            while ((REG16(UART1 + 0x08) & 0x0200) && millis() - start < 100)
                ;
            REG16(UART1 + 0x06) = b;
        }
        /* Its answer, among whatever else it has to say. */
        start = millis();
        while (millis() - start < patience) {
            got = frame(answer, max, &len, patience - (millis() - start));
            if (got == cmd || (got == 0x20 && cmd != 0x20)) {
                radio_answered = got;
                REG16(UART1 + 0x04) = control;
                if (!(got == 0x20 && len && answer[0]))
                    return len;
                radio_refusal = answer[0];
                radio_refusals++;
                break;
            }
        }
        REG16(UART1 + 0x04) = control;
        if (millis() - start >= patience) {
            silent++;
            continue;
        }
        start = millis();
        while (millis() - start < 100)
            if (sd_idle)
                sd_idle();
    }
    return silent < tries ? -2 : -1;
}

int radio_source(int source, int volume)
{
    u8 d[9] = { 0x08, (u8)source, (u8)volume, 0, 0, 0, 0, 0x01, 0x03 }, a[4];
    return radio_command(0x81, d, 9, a, sizeof a) < 0 ? -1 : 0;
}

int radio_volume(int volume)
{
    u8 d[2] = { 0x01, (u8)volume }, a[4];
    return radio_command(0x19, d, 2, a, sizeof a) < 0 ? -1 : 0;
}

/* The tuner, as the stock software asks for a frequency: in steps of
 * 50 kHz on FM (2042 is 102.1 MHz).  Seen from the stock software in
 * the emulator, whose stand-in for the radio processor does not answer
 * it as the real one must (the software repeats it five times there);
 * what a real one answers is for a unit to say. */
int radio_tune(int frequency)
{
    u8 d[4] = { 0x03, 0x02, (u8)(frequency >> 8), (u8)frequency }, a[8];
    return radio_command(0x82, d, 4, a, sizeof a) < 0 ? -1 : 0;
}

/* The radio processor's status: 57 bytes, of which bytes 4 and 5 are
 * the tuned frequency and 33 and 34 the amplifier's source and volume.
 * The number of bytes, or negative. */
int radio_status(u8 *status, int max)
{
    return radio_command(0x88, 0, 0, status, max);
}

/* The radio processor's line and its start, for a program that the
 * boot ROM starts (from flash) and that finds neither as the installed
 * software leaves them.  The line as that software and the boot ROM
 * set it: 9600 bit/s, eight bits, no parity (mode 0: not the
 * faceplate's format, which has a parity bit; a unit's radio processor
 * answered nothing sent with that).  And then the
 * frames that software sends at every start, in its order: what each
 * of them is for is known of few (0x0b asks for the version, 0x88 for
 * the status, 0x19 [01 n] would be the volume), so they are sent as
 * they are.  Harmless after the installed software has done the same.
 * Returns how many of them were not taken; radio_start_log has, for
 * each frame in order, 0x8000 if it was not, how often it was refused
 * in bits 8 to 14 and the last refusal's byte. */
u16 radio_start_log[19];

int radio_start(void)
{
    static const u8 frames[][5] = {
        /* command, length, data */
        { 0x80, 2, 0x00, 0x02 }, { 0xdd, 0 }, { 0x0b, 0 }, { 0xd1, 0 }, { 0x88, 0 }, { 0x8a, 0 },
        { 0x80, 2, 0x02, 0x01 }, { 0x17, 1, 0x01 }, { 0x19, 3, 0x07, 0x03, 0x0a }, { 0xd1, 0 },
        { 0x19, 3, 0x07, 0x00, 0x17 }, { 0xd9, 0 },
    };
    static const u8 later[][5] = {
        { 0x81, 2, 0x0a, 0x0d }, { 0xd0, 0 }, { 0x81, 1, 0x06 }, { 0xde, 1, 0x01 }, { 0x80, 2, 0x08, 0x00 }, { 0x81, 2, 0x0a, 0x02 },
    };
    u8 answer[64];
    u32 i;
    int silent = 0, r, before;
    if (screen_width != 400)
        return -1;
    /* (Register for register as the stock software's set-up, but for
     * the receive interrupt, which nobody would take here.) */
    REG16(UART1 + 0x04) = 0;
    REG16(UART1 + 0x00) = 0x0000;
    REG16(UART1 + 0x02) = 0x005f;
    REG16(UART1 + 0x0c) = 0x0000;
    REG16(UART1 + 0x0e) = 0x0000;
    REG16(UART1 + 0x08) = 0x0104;
    REG16(UART1 + 0x0c) = 0x8000;
    REG16(UART1 + 0x0c) = 0x0000;
    REG16(UART1 + 0x04) = 0x0018; /* the receiver; the transmitter for each frame */
    for (i = 0; i < sizeof frames / sizeof frames[0]; i++) {
        before = radio_refusals;
        /* (0x8a is the one a unit's radio processor says nothing to:
         * sent, as the stock software sends it, and not waited for.) */
        if (frames[i][0] == 0x8a)
            patience = 100, tries = 1;
        r = radio_command(frames[i][0], frames[i] + 2, frames[i][1], answer, sizeof answer);
        if (frames[i][0] == 0x8a)
            patience = 600, tries = 3, r = r < 0 ? 0 : r;
        radio_start_log[i] = (u16)((r < 0 ? 0x8000 : 0) | (radio_refusals - before) << 8 | (radio_refusals != before ? radio_refusal & 0xff : 0));
        if (r < 0)
            silent++;
        /* A radio processor that hears nothing is not waited for
         * nineteen times over (two seconds each). */
        if (i == 1 && silent == 2)
            return 99;
    }
    before = radio_refusals;
    r = radio_source(RADIO_TUNER, 7);
    radio_start_log[12] = (u16)((r < 0 ? 0x8000 : 0) | (radio_refusals - before) << 8 | (radio_refusals != before ? radio_refusal & 0xff : 0));
    if (r)
        silent++;
    for (i = 0; i < sizeof later / sizeof later[0]; i++) {
        before = radio_refusals;
        r = radio_command(later[i][0], later[i] + 2, later[i][1], answer, sizeof answer);
        radio_start_log[13 + i] = (u16)((r < 0 ? 0x8000 : 0) | (radio_refusals - before) << 8 | (radio_refusals != before ? radio_refusal & 0xff : 0));
        if (r < 0)
            silent++;
    }
    return silent;
}
