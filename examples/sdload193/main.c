/* The SD loader for the 193, as the 192's (examples/sdboot) and more:
 * on a disc in the loader's place, started by the stock software (the
 * disc's index higher than the unit's), it stays as the loader the
 * programs return to.
 *
 *   - A menu of the programs, the files NAME.BIN in the top directory of
 *     the card in either slot (RNSE.BIN first) that fit the 193's place
 *     (1 MiB): the select knob, NEXT and PREV choose, SELECT runs.  A
 *     program that is not a 193 program says so when chosen.
 *   - RETURN held for two seconds in a program comes back here, with
 *     that program chosen; RETURN then runs it again.  RETURN held for
 *     two seconds here goes back to the stock software: the display
 *     opens, the disc is ejected, and the unit starts anew (no flash is
 *     written).
 *   - A program pushed over CAN (tools/canbench.py run ... --push FILE,
 *     the kit's canload) starts at once, as if chosen here: nothing to
 *     swap, no reset (the 193 asks for its PIN at every start).
 *   - The key next to the power button opens and closes the display,
 *     CD EJECT ejects the disc, and the radio processor gets its
 *     keep-alive (panel_service), wherever the loader waits.  There is
 *     no way from here to the stock updater or to any writing of the
 *     flash (CLAUDE.md, "Mandatory hardware behavior").
 *
 * Its reports go out on CAN through the controller as the stock software
 * left it (mailbox 1, no reset: the kit's can_init left the bench 193's
 * bus silent), and receiving takes it as it is too (can_attach).
 *
 * The card is read through the chip's SD host (src/hw193.c, sdhi_*),
 * one data line, polled.  Records on 0x7F8 (test, item, value, second):
 *
 *   0x70 n     alive (value: SR; n bit 7: this build, bit 0: back from a
 *              program)
 *   0x55 s     board_init at its step s
 *   0x71 c     an SD command (item: its word; value: argument; second:
 *              the low half of the response); 0x72 c its result and
 *              the response's high half, when the result is not 0
 *   0x77 n     a pin register (n: 0..4 before, 0x10..0x14 after the
 *              stock software's settings, sdhi_pins): its address; 0x78
 *              n its value
 *   0x73 slot  sd_init's result for that slot
 *   0x74 r     sd_load's result; value: the file's size; second: the
 *              slot, plus 0x10 for LINKBENC.BIN
 *   0x75 0     CRC-32 of what was loaded, then the program is started
 *   0x76 n     a look at the slots: n programs found
 *   0x7a 0     back from a program: value its status
 *   0x7a 1     going back to the stock software
 *   0x7a 2     a program not started: value why (loader_run's answer,
 *              or -20 not a 193 program, or the load's error) */
#include "rnse.h"

extern char _load[]; /* link.ld: the loader's place */
#define CAN 0xffdf0000
#define CAN_MB1 (CAN + 0x100 + 0x20)
#define WATCHDOG 0xffc44008
#define BUFFER ((u8 *)0x8c000000) /* RAM well away from this program */
#define MAX_PROGRAM 0x100000

static int ticking;
static u32 strobe;

static void feed(void)
{
    if (!ticking) {
        strobe ^= 1;
        REG32(WATCHDOG) = (REG32(WATCHDOG) & ~1u) | strobe;
    }
}

/* A frame on mailbox 1 with a counted wait. */
static void say(u8 test, u8 item, u32 value, u32 second)
{
    u8 d[8] = { test, item, (u8)(value >> 24), (u8)(value >> 16), (u8)(value >> 8), (u8)value, (u8)(second >> 8), (u8)second };
    u32 n;
    int i;
    REG16(CAN_MB1 + 0x00) = (u16)(0x7f8 << 2);
    for (i = 0; i < 8; i++)
        REG8(CAN_MB1 + 0x08 + i) = d[i];
    REG16(CAN_MB1 + 0x10) = 8;
    REG32(CAN + 0x20) = 0x00000002;
    feed();
    for (n = 0; n < 400000; n++)
        if (REG16(CAN + 0x32) & 0x0002)
            break;
    if (REG16(CAN + 0x32) & 0x0002) {
        REG16(CAN + 0x32) = 0x0002;
    } else {
        REG32(CAN + 0x28) = 0x00000002;
        for (n = 0; n < 400000; n++)
            if (!(REG16(CAN + 0x22) & 0x0002) || (REG16(CAN + 0x3a) & 0x0002))
                break;
        REG16(CAN + 0x3a) = 0x0002;
    }
    feed();
}

static void step(int s)
{
    say(0x55, (u8)s, 0, 0);
}

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

/* ---- the menu ------------------------------------------------------ */

#define MAX_FILES 12
#define FROM_MENU 1
#define FROM_CAN 2
#define FIRST_ROW 5

static struct {
    char name[12];
    u8 slot;
} files[MAX_FILES];
static int nfiles, chosen;
static int seen_slot[2];
static const int text_scale = 2;

static void line(int r, const char *text, u8 colour)
{
    int y = 16 + 20 * r;
    screen_fill(0, y - 2, screen_width, 20, 0);
    if (text)
        screen_text(16, y, text, colour, text_scale);
}

static int same(const char *a, const char *b)
{
    int i;
    for (i = 0; i < 11; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static void file_name(char *out, const char *name)
{
    int i;
    for (i = 0; i < 8 && name[i] != ' '; i++)
        *out++ = name[i];
    *out++ = '.';
    for (i = 8; i < 11 && name[i] != ' '; i++)
        *out++ = name[i];
    *out = 0;
}

static char *put_dec(char *p, int v)
{
    char t[12];
    int n = 0;
    u32 u = v < 0 ? (u32)-v : (u32)v;
    if (v < 0)
        *p++ = '-';
    do {
        t[n++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    while (n)
        *p++ = t[--n];
    *p = 0;
    return p;
}

static void draw_menu(void)
{
    char text[40], *p;
    int i;
    line(FIRST_ROW - 1, nfiles ? "KNOB, NEXT, PREV: CHOOSE.  SELECT: RUN" : "NO PROGRAMS ON THE CARDS (NAME.BIN)", 2);
    for (i = 0; i < MAX_FILES; i++) {
        if (i >= nfiles) {
            line(FIRST_ROW + i, 0, 0);
            continue;
        }
        p = text;
        *p++ = i == chosen ? '>' : ' ';
        *p++ = ' ';
        file_name(p, files[i].name);
        while (*p)
            p++;
        *p++ = ' ', *p++ = ' ', *p++ = 'S', *p++ = 'L', *p++ = 'O', *p++ = 'T', *p++ = ' ';
        *p++ = (char)(files[i].slot ? '1' : '2');
        *p = 0;
        line(FIRST_ROW + i, text, i == chosen ? 2 : 1);
    }
}

/* The programs on the cards: slot 1 (select 1) first, then slot 2; a
 * card whose file system does not mount (the SD bridge, to this
 * loader) has none.  Called while nothing runs: a card that went in is
 * found at the next look. */
static void scan(void)
{
    char name[12];
    u32 size;
    int slot, i, k, r;
    nfiles = 0;
    for (k = 0; k < 2; k++) {
        slot = k == 0 ? 1 : 0;
        r = sd_init(slot);
        say(0x73, (u8)slot, (u32)r, 0);
        seen_slot[slot] = r == 0;
        if (r)
            continue;
        for (i = 0; nfiles < MAX_FILES && sd_list(i, name, &size) == 0; i++) {
            if (name[8] != 'B' || name[9] != 'I' || name[10] != 'N' || name[0] == '_')
                continue;
            if (size < 0x20 || size > MAX_PROGRAM - 0x80)
                continue;
            files[nfiles].slot = (u8)slot;
            for (r = 0; r < 12; r++)
                files[nfiles].name[r] = name[r];
            /* RNSE.BIN first: the card's own program. */
            if (same(name, "RNSE    BIN") && nfiles) {
                files[nfiles] = files[0];
                files[0].slot = (u8)slot;
                for (r = 0; r < 12; r++)
                    files[0].name[r] = name[r];
            }
            nfiles++;
        }
    }
    say(0x76, (u8)nfiles, millis(), 0);
    if (chosen >= nfiles)
        chosen = 0;
}

/* A program of the menu, loaded and started in the loader's session;
 * returns (the reason, below 0) only when it was not started. */
static int run_file(int i)
{
    char text[40], *p;
    u32 size;
    int r;
    r = sd_init(files[i].slot);
    if (r == 0)
        r = sd_load(files[i].name, BUFFER, MAX_PROGRAM, &size);
    if (r == 0 && (((u32 *)BUFFER)[0] & 0xfff00000) != ((u32)_load & 0xfff00000))
        r = -20; /* a 192 program, say */
    if (r) {
        say(0x7a, 2, (u32)r, 0);
        p = (char *)text;
        file_name(p, files[i].name);
        while (*p)
            p++;
        if (r == -20) {
            const char *m = ": NOT A 193 PROGRAM";
            while (*m)
                *p++ = *m++;
            *p = 0;
        } else {
            const char *m = ": NOT LOADED ";
            while (*m)
                *p++ = *m++;
            put_dec(p, r);
        }
        line(2, text, 2);
        return r;
    }
    say(0x75, 0, crc32(BUFFER, size), 0);
    line(2, "STARTING IT", 2);
    loader_note(files[i].name, FROM_MENU);
    display_wait(10000); /* a display on its way gets there first */
    r = loader_run(BUFFER, size, files[i].slot, 0);
    say(0x7a, 2, (u32)r, 0);
    line(2, "IT DID NOT START", 2);
    return r;
}

/* canload's way to start a pushed program: in the session, as if it had
 * been chosen here (so that RETURN held comes back). */
static int run_pushed(void *image, u32 bytes)
{
    int r;
    say(0x75, 1, crc32(image, bytes), 0);
    loader_note("CAN PUSH   ", FROM_CAN);
    display_wait(10000);
    r = loader_run(image, bytes, 0, 0);
    say(0x7a, 2, (u32)r, 0);
    line(2, "THE PUSHED PROGRAM DID NOT START", 2);
    return r;
}

/* RETURN held: back to the stock software.  The disc would start this
 * loader again, so it goes out first (the display opened, as it hides
 * the drive); then the unit starts anew: nothing written to the flash. */
static void to_stock(void)
{
    u32 t;
    say(0x7a, 1, 0, 0);
    line(2, "BACK TO THE STOCK SOFTWARE", 2);
    line(3, "OPENING THE DISPLAY", 1);
    display_open();
    if (display_wait(10000) == DISPLAY_OPEN) {
        line(3, "EJECTING THE DISC: TAKE IT OUT", 1);
        cd_eject();
    } else {
        line(3, "THE DISPLAY DID NOT OPEN: DISC LEFT IN", 2);
    }
    t = millis();
    while (millis() - t < 3000) {
        panel_service();
        cpu_idle();
    }
    restart();
}

/* The CAN frames the loader gets: canload's.  While they come, the
 * slots are left alone (a look at an empty slot waits out its time). */
static u32 can_seen;

static void can_frames(void)
{
    u16 id;
    u8 d[8];
    int len;
    while ((len = can_receive(&id, d)) >= 0)
        if (canload_frame(id, d, len))
            can_seen = millis();
}

static void idle(void)
{
    panel_service();
    can_frames();
}

int main(void)
{
    u32 sr, blocked = 0, keys_start, last_scan;
    int r, status = 0, returned, return_armed = 0, i;
    int next_down = 0, prev_down = 0, select_down = 0;
    char noted[12], text[40];
    u32 flags = 0;

    returned = loader_returned(&status, &blocked);
    __asm__ volatile("stc sr, %0" : "=r"(sr));
    /* What the stock software had queued on the bus is taken back. */
    REG32(CAN + 0x28) = 0xfffffffe;
    /* (Item: bit 7 set marks this build, which answers the faceplate in
     * the SD host's waits; bit 0, back from a program.  canbench --push-
     * loader tells by it whether the loader that started is the disc's
     * older one, to be replaced over CAN.) */
    say(0x70, (u8)(0x80 | (returned >= 0)), sr, 0);

    board_init_step = step;
    board_init();
    board_init_step = 0;
    ticking = 1;
    say(0x55, 0xff, millis(), 0);
    console_puts("\nrnse sdk: sdload193\n");
    screen_palette(1, 255, 255, 255);
    screen_palette(2, 255, 160, 0);
    line(0, "SD LOADER 193", 2);
    line(1, "RETURN: RUN AGAIN.  HELD 2 S: STOCK.  CAN PUSH: READY", 1);
    screen_text(16, screen_height - 24, "KEY BY THE POWER BUTTON: DISPLAY.  CD EJECT: DISC OUT", 2, text_scale);
    panel_init();
    /* The faceplate's start answered before any card is touched, as the
     * first loaders did (they stayed lit on a bench 193; one that started
     * its cards at once left the front dark for good). */
    {
        u32 t = millis();
        while (millis() - t < 600) {
            panel_service();
            cpu_idle();
        }
    }
    sd_idle = idle;
    display_idle = idle;
    can_attach();
    canload_init(BUFFER, MAX_PROGRAM);
    canload_start = run_pushed;
    /* RETURN held is this loader's own: back to the stock software. */
    panel_return_to_loader = 0;
    sdhi_trace = 0;
    if (returned >= 0) {
        char *p = text;
        const char *m = status == LOADER_RETURN_HELD ? "BACK: RETURN HELD" : "BACK: THE PROGRAM ENDED, ";
        while (*m)
            *p++ = *m++;
        *p = 0;
        if (status != LOADER_RETURN_HELD)
            put_dec(p, status);
        line(2, text, 1);
        say(0x7a, 0, (u32)status, 0);
    }
    keys_start = millis();
    scan();
    if (loader_noted(noted, &flags))
        for (i = 0; i < nfiles; i++)
            if (same(files[i].name, noted))
                chosen = i;
    draw_menu();
    last_scan = millis();
    for (;;) {
        int knob;
        idle();
        /* RETURN counts once the faceplate answers, a second after the
         * start, and once it has been seen let go: a RETURN still held
         * from the program that just ended is not a new one. */
        if (!return_armed && panel_online() && millis() - keys_start >= 1000 && !panel_key(KEY_RETURN)) {
            return_armed = 1;
            (void)panel_return_short();
            (void)panel_return_held();
        }
        if (return_armed && panel_return_held())
            to_stock();
        if (return_armed && panel_return_short() && nfiles) {
            r = run_file(chosen);
            draw_menu();
        }
        knob = panel_knob(KNOB_SELECT);
        if (panel_key(KEY_NEXT) && !next_down)
            knob++;
        if (panel_key(KEY_PREV) && !prev_down)
            knob--;
        if (knob && nfiles) {
            chosen = (int)umod((u32)(chosen + knob + 8 * nfiles), (u32)nfiles);
            draw_menu();
        }
        if (panel_key(KEY_SELECT) && !select_down && nfiles) {
            r = run_file(chosen);
            draw_menu();
        }
        next_down = panel_key(KEY_NEXT);
        prev_down = panel_key(KEY_PREV);
        select_down = panel_key(KEY_SELECT);
        /* A look at the slots every few seconds while the menu waits: a
         * card that went in (or came out) shows up. */
        if (millis() - last_scan >= 4000 && millis() - can_seen >= 3000) {
            int before = nfiles;
            scan();
            if (nfiles != before)
                draw_menu();
            last_scan = millis();
        }
        cpu_idle();
    }
    (void)r;
    return 0;
}
