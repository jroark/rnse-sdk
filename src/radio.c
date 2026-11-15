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

int radio_command(u8 cmd, const u8 *data, int n, u8 *answer, int max)
{
    int try, i, got, len = 0;
    if (screen_width != 400)
        return -1; /* the 193's radio is spoken to differently, and not from here */
    for (try = 0; try < 3; try++) {
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
        while (millis() - start < 600) {
            got = frame(answer, max, &len, 600 - (millis() - start));
            if (got == cmd || (got == 0x20 && cmd != 0x20)) {
                radio_answered = got;
                REG16(UART1 + 0x04) = control;
                return got == 0x20 && len && answer[0] ? -2 : len;
            }
        }
        REG16(UART1 + 0x04) = control;
    }
    return -1;
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
