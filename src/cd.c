/* The CD/DVD drive by programmed I/O, and just enough of ISO 9660 to
 * find a file in the root directory.
 *
 * The steps are those of the ATAPI packet protocol and of the 193's
 * stock loader, which works the same way: select the drive, set the
 * byte count limit, command 0xA0, wait for DRQ, twelve bytes of packet,
 * then for each block the drive offers: byte count, data. */
#include "rnse.h"

#define BSY 0x80
#define DRQ 0x08
#define ERR 0x01

static u8 sector_buf[2048];

static int wait_clear(u8 bits, u32 ms)
{
    u32 start = millis();
    while (cd_alt_status() & bits)
        if (millis() - start > ms)
            return -1;
    return 0;
}

int cd_command(const u8 *command, void *buf, u32 len)
{
    u8 *out = buf;
    u32 got = 0;
    int i;

    if (wait_clear(BSY, 5000))
        return -1;
    cd_reg_write(6, 0xa0);
    if (wait_clear(BSY | DRQ, 5000))
        return -1;
    cd_reg_write(1, 0x00); /* no DMA */
    cd_reg_write(4, 0x00); /* at most 2048 bytes a block */
    cd_reg_write(5, 0x08);
    cd_reg_write(7, 0xa0); /* PACKET */
    cd_alt_status();       /* gives the drive time to raise BSY */
    if (wait_clear(BSY, 5000))
        return -1;
    if (!(cd_alt_status() & DRQ))
        return 0x100 | cd_reg_read(1);
    for (i = 0; i < 12; i += 2)
        cd_data_write((u16)(command[i] << 8 | command[i + 1]));
    for (;;) {
        u8 status;
        u32 n, k;
        cd_alt_status();
        if (wait_clear(BSY, 30000))
            return -1;
        status = cd_reg_read(7);
        if (status & ERR)
            return 0x100 | cd_reg_read(1);
        if (!(status & DRQ))
            return 0;
        n = (u32)cd_reg_read(4) | (u32)cd_reg_read(5) << 8;
        for (k = 0; k < n; k += 2) {
            u16 w = cd_data_read();
            if (got < len)
                out[got++] = (u8)(w >> 8);
            if (got < len)
                out[got++] = (u8)w;
        }
    }
}

static int port_ready;

int cd_init(void)
{
    cd_port_init();
    port_ready = 1;
    return wait_clear(BSY, 5000);
}

int cd_wait_ready(u32 ms)
{
    static const u8 test_unit_ready[12] = { 0x00 };
    static const u8 request_sense[12] = { 0x03, 0, 0, 0, 18 };
    static const u8 start_unit[12] = { 0x1b, 0, 0, 0, 1 };
    u32 start = millis(), started = start - 1000;
    for (;;) {
        u8 sense[18];
        int r = cd_command(test_unit_ready, 0, 0);
        if (r <= 0)
            return r;
        /* Not ready, or the disc has changed: the drive wants to be
         * asked why before it says anything else. */
        if (cd_command(request_sense, sense, sizeof sense) == 0 && (sense[2] & 15) == 2 && sense[12] == 0x04) {
            /* Not ready, with a disc: the 192's drive does not turn a
             * disc it has just loaded until it is told to (on a unit
             * it said 2/04/04 for as long as it was only asked), and
             * the stock software tells it whenever it is not ready. */
            if (millis() - started > 500) {
                cd_command(start_unit, 0, 0);
                started = millis();
            }
        }
        if (millis() - start > ms)
            return r;
        delay_ms(100);
    }
}

int cd_read(u32 sector, u32 count, void *buf)
{
    u8 *out = buf;
    while (count) {
        u32 n = count > 16 ? 16 : count;
        u8 read10[12] = { 0x28, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        int r;
        read10[2] = (u8)(sector >> 24);
        read10[3] = (u8)(sector >> 16);
        read10[4] = (u8)(sector >> 8);
        read10[5] = (u8)sector;
        read10[8] = (u8)n;
        r = cd_command(read10, out, n * 2048);
        if (r)
            return r;
        sector += n;
        count -= n;
        out += n * 2048;
    }
    return 0;
}

static u32 le32(const u8 *p)
{
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

int cd_find(const char *name, u32 *sector, u32 *size)
{
    u32 dir, left;
    int r = cd_read(16, 1, sector_buf);
    if (r)
        return r;
    /* Primary volume descriptor: type 1, "CD001". */
    if (sector_buf[0] != 1 || sector_buf[1] != 'C' || sector_buf[2] != 'D' || sector_buf[5] != '1')
        return -2;
    /* Its root directory record is at 156: extent at +2, length at +10. */
    dir = le32(sector_buf + 156 + 2);
    left = le32(sector_buf + 156 + 10);
    for (; left; dir++, left -= left > 2048 ? 2048 : left) {
        u32 at = 0;
        r = cd_read(dir, 1, sector_buf);
        if (r)
            return r;
        /* Records do not cross sectors; a zero length ends a sector. */
        while (at < 2048 && sector_buf[at]) {
            const u8 *rec = sector_buf + at;
            u32 n = rec[32], i = 0;
            if (!(rec[25] & 2)) { /* not a directory */
                while (i < n && name[i] && rec[33 + i] == (u8)name[i])
                    i++;
                if (!name[i] && (i == n || rec[33 + i] == ';')) {
                    *sector = le32(rec + 2);
                    *size = le32(rec + 10);
                    return 0;
                }
            }
            at += rec[0];
        }
    }
    return -3;
}

/* The disc out: the drive allowed to let it go (the stock software
 * forbids removal) and told to eject, as sdk/examples/base does it.
 * The drive's port is set up first if cd_init has not done so. */

int cd_eject(void)
{
    static const u8 allow_removal[12] = { 0x1e, 0, 0, 0, 0 };
    static const u8 eject[12] = { 0x1b, 1, 0, 0, 2 };
    if (!port_ready) {
        cd_port_init();
        port_ready = 1;
    }
    cd_command(allow_removal, 0, 0);
    return cd_command(eject, 0, 0);
}

int cd_follow_key(void)
{
    static int was;
    int down = panel_key(KEY_CD_EJECT), pressed = down && !was;
    was = down;
    if (pressed)
        cd_eject();
    return pressed;
}

/* RETURN: held for two seconds, back to the SD loader (CLAUDE.md,
 * "Mandatory hardware behavior"); released sooner, a short press for
 * the program.  A RETURN that is already down when the program starts
 * (the one that brought it back, say) counts only once it has been let
 * go. */
void (*loader_leave)(void);
int panel_return_to_loader = 1;
static int return_state; /* 0 not yet seen up, 1 up, 2 down, 3 held */
static u32 return_since;
static int return_short, return_held;

static void follow_return(void)
{
    if (!panel_key(KEY_RETURN)) {
        if (return_state == 2)
            return_short = 1;
        return_state = 1;
        return;
    }
    if (return_state == 1) {
        return_state = 2;
        return_since = millis();
    } else if (return_state == 2 && millis() - return_since >= 2000) {
        return_state = 3;
        if (panel_return_to_loader && loader_active()) {
            /* The program's own ending (playback stopped and so on),
             * the display at rest, then the loader; this returns only
             * when the loader's copy is damaged. */
            if (loader_leave)
                loader_leave();
            display_wait(10000);
            loader_return(LOADER_RETURN_HELD);
        }
        return_held = 1;
    }
}

int panel_return_short(void)
{
    int r = return_short;
    return_short = 0;
    return r;
}

int panel_return_held(void)
{
    int r = return_held;
    return_held = 0;
    return r;
}

/* What every program owes the keys (see rnse.h). */
void panel_service(void)
{
    panel_poll();
    display_follow_key();
    cd_follow_key();
    follow_return();
}
