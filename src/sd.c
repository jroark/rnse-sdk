/* SD cards on the 192, through the card processor of a slot.
 *
 * The slots belong to two signal processors (NEC uPD77115) that do
 * nothing until a program is loaded into them, and the card interface
 * is theirs.  This driver has no program of its own for them: it loads
 * the installed software's, **out of the unit's flash**, where it is
 * part of the operating system, and then says to it what the installed
 * software's driver says: start, the card's start-up commands, and for
 * every sector "read it into your memory" and "give me that memory".
 * docs/hardware.md, "SD card processors", has the protocol; the words
 * here are those of the stock driver as the emulator shows them
 * (`rnse boot --sd-card ... --trace-dsp`).
 *
 * So this works with one installed software and no other: the North
 * American V222 0371, whose program sits at 0x00200000 in the flash.
 * The table below says how the stock driver's download is put together
 * from the flash (tools/dsp-recipe.py makes it from a trace and checks
 * that it rebuilds every word); sd_init looks for the program's
 * header first and gives up on a unit that has other software.
 *
 * Reading only.  Standard and high-capacity cards; FAT12, FAT16 and
 * FAT32. */
#include "rnse.h"

#define STATUS 0xb2400001 /* bits 2, 0: nothing to read; 3, 1: cannot write */
#define LATCH 0xb6000004  /* bits 2, 3: the processors' reset lines, low = reset */
static const u32 port[2] = { 0xb5000002, 0xb500004a };

enum { FROM_FLASH, ZEROES, LITERAL, ANSWER };
static const u16 literal[] = { 0x0001, 0x0e00, 0x0c00, 0x0002, 0x1c6f, 0x8000, 0x00f6, 0x9f00, 0x0004, 0x0002, 0x0bfb, 0x0000, 0x0000, 0x026c, 0x2994, 0x0467, 0x4900, 0x0001, 0x4ef2, 0x0001, 0x0002, 0x061d, 0x29e3, 0x0f4f, 0x4000 };
static const struct step { u8 kind; u32 at; u16 count; } recipe[] = {
    { LITERAL, 0x000000, 3 },
    { FROM_FLASH, 0x20000c, 7168 },
    { ANSWER, 0x000000, 0 },
    { LITERAL, 0x000003, 3 },
    { FROM_FLASH, 0x203818, 14558 },
    { ANSWER, 0x000000, 0 },
    { LITERAL, 0x000006, 2 },
    { FROM_FLASH, 0x20a9dc, 492 },
    { ANSWER, 0x000000, 0 },
    { LITERAL, 0x000008, 5 },
    { ANSWER, 0x000000, 0 },
    { LITERAL, 0x00000d, 2 },
    { FROM_FLASH, 0x20adcc, 620 },
    { ANSWER, 0x000000, 0 },
    { LITERAL, 0x00000f, 2 },
    { FROM_FLASH, 0x20b2ac, 1127 },
    { ANSWER, 0x000000, 0 },
    { LITERAL, 0x000011, 3 },
    { ANSWER, 0x000000, 0 },
    { LITERAL, 0x000014, 3 },
    { ZEROES, 0x000000, 58 },
    { FROM_FLASH, 0x20bc04, 1507 },
    { ANSWER, 0x000000, 0 },
    { LITERAL, 0x000017, 2 },
    { ZEROES, 0x000000, 31 },
    { FROM_FLASH, 0x20c810, 3888 },
    { ANSWER, 0x000000, 0 },
};

static int slot = -1;
static int high_capacity;

/* For whoever wants to say why it did not work: the last command's
 * register value, the header of what the processor answered (0xee..
 * is its "failed", with a code in the first word), and two words of
 * the answer. */
u16 sd_last[4];

/* The stock driver leaves 150 ms between any two steps of a card's
 * start, and a real card wants the time. */
void (*sd_idle)(void);

static void pause(void)
{
    u32 start = millis();
    /* (A card's start is four seconds of these, and the faceplate
     * controller, which has the screen's light, wants answering in
     * the meantime: whoever has one sets sd_idle to panel_poll.) */
    while (millis() - start < 150)
        if (sd_idle)
            sd_idle();
}

static int wait_low(u8 mask, u32 ms)
{
    u32 start = millis();
    while (REG8(STATUS) & mask)
        if (millis() - start > ms)
            return -1;
    return 0;
}

/* Words the processor said while the host was waiting to say one of
 * its own.  The stock driver takes the processor's words in an
 * interrupt; a driver that only listens when it expects an answer and
 * otherwise waits to write can wait for ever, each side with a word
 * for the other that is not taken (a unit showed that three seconds
 * into the first file it played).  So whatever is there to read while
 * the host waits to write is taken and kept for the next get. */
static u16 inbox[64];
static u32 inbox_in, inbox_out;

/* The last words in either direction, for whoever has to say what went
 * wrong on a unit: 0x8000 set in sd_heard_from for the host's. */
u16 sd_heard[48];
u8 sd_heard_host[48];
u32 sd_heard_n;

static void note(u16 w, int host)
{
    sd_heard[sd_heard_n % 48] = w;
    sd_heard_host[sd_heard_n++ % 48] = (u8)host;
}

static int put(u16 w)
{
    u32 start = millis(), t;
    u8 full = slot ? 2 : 8, none = slot ? 1 : 4;
    /* (Generous: the program takes its time over its own start,
     * after the last word of the download.) */
    for (;;) {
        u8 status = REG8(STATUS);
        if (!(status & none) && inbox_in - inbox_out < 64) {
            u16 got = REG16(port[slot]);
            inbox[inbox_in++ % 64] = got;
            note(got, 0);
            continue;
        }
        if (!(status & full))
            break;
        if (millis() - start > 3000)
            return -1;
    }
    /* Not at once.  The processor's program takes most words in its
     * host interrupt, but the words that follow a message's head it
     * fetches itself with the interrupt masked, and then lets the next
     * interrupt pass as the one that belonged to the fetched word
     * (its routine at 0x0381, flag 0x0a61).  A word that arrives
     * between the fetch and the unmasking, eight instructions, has no
     * interrupt of its own then and is never taken: the processor
     * waits and so does the host.  So the program gets the time to be
     * past that before the next word. */
    t = micros();
    while (micros() - t < 20)
        ;
    REG16(port[slot]) = w;
    note(w, 1);
    return 0;
}

static int get(u16 *w, u32 ms)
{
    if (inbox_in != inbox_out) {
        *w = inbox[inbox_out++ % 64];
        return 0;
    }
    if (wait_low(slot ? 1 : 4, ms))
        return -1;
    *w = REG16(port[slot]);
    note(*w, 0);
    return 0;
}

static int answer(u16 *out, int max, u32 ms)
{
    u16 head, w;
    int i;
    if (get(&head, ms))
        return -1;
    for (i = 0; i < (head & 0xff); i++) {
        if (get(&w, 300))
            return -1;
        if (i < max)
            out[i] = w;
    }
    return head;
}

/* A message of `n` words whose answer is the plain "done" (0xf000). */
static int say(const u16 *m, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (put(m[i]))
            return -1;
    return answer(0, 0, 1000) == 0xf000 ? 0 : -1;
}

/* A word of the processor's memory set: 0 its registers, 1 its data. */
static int poke(u16 space, u16 addr, u16 value)
{
    u16 m[4] = { 0x2303, space, addr, 1 };
    if (say(m, 4))
        return -1;
    return put(value);
}

/* A command to the card: the interface's command register value and
 * the argument.  The card's response, as the processor hands it on, is
 * in `r` (up to nine words); 0 when one came. */
static int command(u16 head, u16 reg, u32 arg, u16 *r, int expect)
{
    u16 m[4] = { head, reg, (u16)(arg >> 16), (u16)arg };
    int i, got;
    for (i = 0; i < 9; i++)
        r[i] = 0;
    sd_last[0] = reg;
    sd_last[1] = sd_last[2] = sd_last[3] = 0;
    if (say(m, 4))
        return -1;
    if (!expect)
        return 0;
    got = answer(r, 9, 2000);
    sd_last[1] = (u16)got;
    sd_last[2] = r[0];
    sd_last[3] = r[1];
    return got < 0 || (got >> 8) == 0xee ? -1 : 0;
}

static u16 flash_word(u32 at)
{
    const u8 *p = (const u8 *)(0x80000000 + at);
    return (u16)(p[0] | p[1] << 8);
}

/* The installed software's program into the processor, as its driver
 * sends it. */
static int download(void)
{
    u32 i, k;
    u16 w;
    for (i = 0; i < sizeof recipe / sizeof recipe[0]; i++) {
        const struct step *s = &recipe[i];
        if (s->kind == ANSWER) {
            if (get(&w, 2000))
                return -1;
            continue;
        }
        for (k = 0; k < s->count; k++) {
            w = s->kind == FROM_FLASH ? flash_word(s->at + 2 * k) : s->kind == LITERAL ? literal[s->at + k] : 0;
            if (put(w))
                return -1;
        }
    }
    return 0;
}

int sd_init(int which)
{
    static const u16 start[] = { 0x1202, 0xd762, 0x0034 };
    static const u16 a[] = { 0x7301, 0x0002 }, b[] = { 0x3301, 0x0001 }, c[] = { 0x3101, 0x0001 };
    static const u16 d[] = { 0x3201, 0x0000 }, e[] = { 0x0301, 0x0000 }, f[] = { 0x0301, 0x0001 }, g[] = { 0x7400 };
    u16 r[9], rca;
    u32 t, ocr = 0, hcs, i;
    int mmc = 0;

    if (screen_width != 400)
        return -1; /* the 193 has a card interface of its own, and no driver here */
    /* The program this driver knows how to send, where it knows it. */
    if (flash_word(0x200000) != 1 || flash_word(0x200004) != 0x3800 || flash_word(0x20380c) != 2 || flash_word(0x203810) != 0x71bc)
        return -2;
    slot = which & 1;
    high_capacity = 0;
    REG32(LATCH) &= ~(4u << slot);
    delay_ms(20);
    REG32(LATCH) |= 4u << slot;
    delay_ms(20);
    if (download())
        return -3;
    /* The program starts itself up now, and a word sent into that is
     * lost with the host interface none the wiser. */
    delay_ms(300);
    if (say(start, 3) || say(a, 2) || say(b, 2) || say(c, 2))
        return -4;
    for (i = 0; i < 2; i++)
        if (put(d[i]))
            return -4;
    if (answer(r, 9, 1000) < 0)
        return -4;
    /* The card interface: its slow clock, and the card's start, a step
     * every 150 ms as the stock driver has it. */
    pause();
    if (poke(0, 0x3814, 0x3802))
        return -5;
    pause();
    if (poke(1, 0x4fb1, 0x0100))
        return -5;
    pause();
    if (say(e, 2))
        return -5;
    pause();
    if (command(0x0b03, 0x4800, 0, r, 0))          /* CMD0 */
        return -6;
    pause();
    /* CMD8 is answered by cards of version 2 of the specification
     * only; to the others the host does not claim to know
     * high-capacity cards. */
    hcs = command(0x0b03, 0x4c08, 0x000001aa, r, 1) ? 0 : 0x40000000;
    /* The first application command.  A card that leaves it unanswered
     * the stock driver starts anew and asks CMD1, a MultiMediaCard's
     * way; with no answer to that either, CMD55 again, and so round and
     * round.  (A unit's 512 MB card, which the stock software reads,
     * left the first CMD55 unanswered.) */
    for (i = 0;; i++) {
        pause();
        if (!command(0x0b03, 0x4c37, 0, r, 1))     /* CMD55 */
            break;
        if (i == 6)
            return -7;
        pause();
        if (command(0x0b03, 0x4800, 0, r, 0))      /* CMD0 */
            return -6;
        pause();
        if (!command(0x0b03, 0x4c01, 0x00ff0000, r, 1)) { /* CMD1 */
            mmc = 1;
            break;
        }
    }
    t = millis();
    for (i = 0;; i++) {
        if (mmc) {
            /* (The first answer is there already.) */
            if (i) {
                delay_ms(300);
                if (command(0x0b03, 0x4c01, 0x00ff0000, r, 1))
                    return -7;
            }
        } else {
            if (i) {
                pause();
                if (command(0x0b03, 0x4c37, 0, r, 1))
                    return -7;
            }
            pause();
            if (command(0x0b03, 0x4c29, hcs | 0x00ff0000, r, 1)) /* ACMD41 */
                return -7;
        }
        ocr = (u32)(r[0] & 0xff) << 24 | (u32)r[1] << 8 | r[2] >> 8;
        if (ocr & 0x80000000)
            break;
        if (millis() - t > 6000)
            return -8;
        if (!mmc) {
            pause();
            if (poke(0, 0x3814, 0x3802))
                return -5;
        }
    }
    high_capacity = !mmc && (ocr & 0x40000000) != 0;
    if (!mmc && hcs && !high_capacity) {
        /* As the stock driver does for a standard card that answered
         * CMD8: once more without the claim. */
        pause();
        if (command(0x0b03, 0x4c37, 0, r, 1))
            return -7;
        pause();
        if (command(0x0b03, 0x4c29, 0x00ff0000, r, 1))
            return -7;
    }
    pause();
    if (poke(0, 0x3814, 0x3802))
        return -5;
    pause();
    if (command(0x0b03, 0x4c42, 0, r, 1))          /* CMD2 */
        return -9;
    pause();
    if (command(0x0b03, 0x4c03, 0, r, 1))          /* CMD3: the card's address */
        return -9;
    /* (A MultiMediaCard takes the address the host says, and the stock
     * driver says none: 0, with which it then selects the card too.
     * Whether such a card takes that is not known.) */
    rca = mmc ? 0 : (u16)((r[0] & 0xff) << 8 | r[1] >> 8);
    pause();
    if (command(0x0b03, 0x4c49, (u32)rca << 16, r, 1)) /* CMD9 */
        return -9;
    pause();
    if (command(0x0b03, 0x4c4a, (u32)rca << 16, r, 1)) /* CMD10 */
        return -9;
    /* The fast clock, the card selected, sectors of 512 bytes. */
    pause();
    if (poke(0, 0x3814, 0x0c02))
        return -5;
    pause();
    if (poke(1, 0x4fb1, 0x0004))
        return -5;
    pause();
    if (command(0x0b03, 0x4c07, (u32)rca << 16, r, 1)) /* CMD7 */
        return -10;
    pause();
    if (command(0x0b03, 0x4c10, 0x200, r, 1))      /* CMD16 */
        return -10;
    pause();
    if (say(f, 2))
        return -11;
    pause();
    if (put(g[0]) || answer(r, 9, 1000) < 0)
        return -11;
    return 0;
}

int sd_read(u32 sector, void *buf)
{
    static const u16 fetch[] = { 0x2203, 0x0001, 0x5000, 0x0100 };
    u8 *out = buf;
    u16 r[9], w;
    int i;
    if (slot < 0)
        return -1;
    /* Where in its memory the processor is to put the sector, and how
     * many words; then the card's read command (CMD17). */
    if (poke(1, 0x4fb6, 0x5000) || poke(1, 0x4fb5, 0x0100))
        return -1;
    if (command(0x0503, 0x4d11, high_capacity ? sector : sector << 9, r, 1))
        return -2;
    /* And that memory, 256 words. */
    for (i = 0; i < 4; i++)
        if (put(fetch[i]))
            return -3;
    if (get(&w, 1000) || w != 0xd301 || get(&w, 300))
        return -3;
    for (i = 0; i < 256; i++) {
        if (get(&w, 300))
            return -3;
        out[2 * i] = (u8)(w >> 8);
        out[2 * i + 1] = (u8)w;
    }
    return 0;
}

/* ---- FAT16 and FAT32, as far as finding a file in the root directory
 * and reading it goes. */

static u8 sec[512];
static u32 fat_start, data_start, root_start, root_entries, per_cluster;
static int fat32, fat12;

static u32 le16(const u8 *p) { return (u32)(p[0] | p[1] << 8); }
static u32 le32(const u8 *p) { return le16(p) | le16(p + 2) << 16; }

/* The card as the installed software left it: that software has sent
 * the processor its program and started the card long before it
 * starts a loader, and both are still as they were.  Nothing is reset
 * and nothing started here; words the processor still had for the old
 * driver are thrown away and a sector is asked for.  This is the way
 * to a card that sd_init cannot start (a unit has one, 512 MB, which
 * the stock software reads), and it takes no time.
 *
 * Whether the card counts in sectors or bytes is found by trying: the
 * first partition's first sector has to look like one. */
static int looks_like_a_volume(void)
{
    return sec[510] == 0x55 && sec[511] == 0xaa && le16(sec + 11) == 512 && sec[13] != 0;
}

int sd_attach(int which)
{
    u16 w;
    u32 first;
    int i;
    if (screen_width != 400)
        return -1;
    if (flash_word(0x200000) != 1 || flash_word(0x200004) != 0x3800 || flash_word(0x20380c) != 2 || flash_word(0x203810) != 0x71bc)
        return -2;
    slot = which & 1;
    high_capacity = 0;
    for (i = 0; i < 600 && get(&w, 20) == 0; i++)
        ;
    if (sd_read(0, sec))
        return -12;
    if (sec[510] != 0x55 || sec[511] != 0xaa)
        return -13;
    if (looks_like_a_volume())
        return 0; /* no partition table: nothing to tell by */
    first = le32(sec + 446 + 8);
    if (first == 0)
        return -13;
    if (sd_read(first, sec) == 0 && looks_like_a_volume())
        return 0;
    high_capacity = 1;
    if (sd_read(first, sec) == 0 && looks_like_a_volume())
        return 0;
    return -13;
}

static int mount(void)
{
    u32 first = 0, fats, per_fat, reserved, total, clusters;
    if (sd_read(0, sec))
        return -1;
    if (sec[510] != 0x55 || sec[511] != 0xaa)
        return -2;
    /* A partition table, or the file system right away. */
    if (!(sec[0] == 0xeb || sec[0] == 0xe9) || le16(sec + 11) != 512) {
        first = le32(sec + 446 + 8);
        if (sd_read(first, sec))
            return -1;
        if (le16(sec + 11) != 512)
            return -2;
    }
    per_cluster = sec[13];
    reserved = le16(sec + 14);
    fats = sec[16];
    root_entries = le16(sec + 17);
    per_fat = le16(sec + 22) ? le16(sec + 22) : le32(sec + 36);
    total = le16(sec + 19) ? le16(sec + 19) : le32(sec + 32);
    if (!per_cluster || !fats)
        return -2;
    fat_start = first + reserved;
    root_start = fat_start + fats * per_fat;
    data_start = root_start + (root_entries * 32 + 511) / 512;
    clusters = udiv(total - (data_start - first), per_cluster);
    fat32 = clusters >= 65525;
    fat12 = clusters < 4085;
    if (fat32)
        root_start = le32(sec + 44); /* a cluster, not a sector */
    return 0;
}

static u32 next_cluster(u32 c)
{
    u32 at = fat32 ? c * 4 : fat12 ? c + c / 2 : c * 2, v;
    if (sd_read(fat_start + at / 512, sec))
        return 0xffffffff;
    if (!fat12)
        return fat32 ? le32(sec + (at & 511)) & 0x0fffffff : le16(sec + (at & 511));
    /* Twelve bits, which may lie across two sectors. */
    v = sec[at & 511];
    if ((at & 511) == 511) {
        if (sd_read(fat_start + at / 512 + 1, sec))
            return 0xffffffff;
        v |= (u32)sec[0] << 8;
    } else {
        v |= (u32)sec[(at & 511) + 1] << 8;
    }
    return c & 1 ? v >> 4 : v & 0xfff;
}

static int last(u32 c)
{
    return c < 2 || c >= (fat32 ? 0x0ffffff8u : fat12 ? 0xff8u : 0xfff8u);
}

/* The entry of `name` (eleven characters, as in the directory) in a
 * sector of directory entries: its first cluster and size.  1 found, 0
 * not here, 2 the directory ends here. */
/* (With `listing` not negative it is not a name that is looked for
 * but the file of that number, whose name is noted.) */
static int listing = -1;
static char listed[12];

static int look(const char *name, u32 *cluster, u32 *size)
{
    int e, i;
    for (e = 0; e < 512; e += 32) {
        if (sec[e] == 0)
            return 2;
        if (sec[e] == 0xe5 || (sec[e + 11] & 0x18))
            continue;
        if (listing >= 0) {
            i = listing-- == 0 ? 11 : 0;
            if (i == 11)
                for (i = 0; i < 11; i++)
                    listed[i] = (char)sec[e + i];
        } else {
            for (i = 0; i < 11 && sec[e + i] == (u8)name[i]; i++)
                ;
        }
        if (i == 11) {
            *cluster = le16(sec + e + 26) | (fat32 ? le16(sec + e + 20) << 16 : 0);
            *size = le32(sec + e + 28);
            return 1;
        }
    }
    return 0;
}

/* The file of that name in the top directory: its first cluster and
 * its size. */
static int find(const char *name, u32 *cluster, u32 *size)
{
    u32 s, c;
    int r = 0;
    if (mount())
        return -1;
    if (fat32) {
        for (c = root_start; !last(c) && r == 0; c = next_cluster(c))
            for (s = 0; s < per_cluster && r == 0; s++) {
                if (sd_read(data_start + (c - 2) * per_cluster + s, sec))
                    return -1;
                r = look(name, cluster, size);
            }
    } else {
        for (s = 0; s < (root_entries + 15) / 16 && r == 0; s++) {
            if (sd_read(root_start + s, sec))
                return -1;
            r = look(name, cluster, size);
        }
    }
    return r == 1 ? 0 : -2;
}

int sd_list(int index, char *name, u32 *size)
{
    u32 cluster;
    int r, i;
    listing = index;
    r = find("", &cluster, size);
    listing = -1;
    if (r)
        return r;
    for (i = 0; i < 12; i++)
        name[i] = listed[i];
    return 0;
}

int sd_load(const char *name, void *to, u32 max, u32 *size)
{
    u32 cluster = 0, s, c, done = 0;
    u8 *out = to;
    int r = find(name, &cluster, size);
    if (r)
        return r;
    if (*size > max)
        return -3;
    for (c = cluster; done < *size; c = next_cluster(c)) {
        if (last(c))
            return -4;
        for (s = 0; s < per_cluster && done < *size; s++) {
            /* (The table of clusters shares the sector buffer: read
             * the data straight to where it goes.) */
            if (sd_read(data_start + (c - 2) * per_cluster + s, out + done))
                return -1;
            done += 512;
        }
    }
    return 0;
}

/* Sound.  The card processor's program is an MP3 player as well: told
 * where on the card a file's bytes are, it reads them itself, decodes
 * and sends the samples to its converter.  The host only feeds it the
 * places, 5000 bytes at a time, each time it asks (0xc701).  These are
 * the stock driver's messages, as it sends them for a file it plays;
 * which of the amplifier's inputs the converter is, and how loud, is
 * the radio processor's business (radio_source). */
static struct {
    u32 sector, sectors;
} extent[48];
static int extents, playing;
static u32 play_at, play_size;

static int peek(u16 space, u16 addr, u16 *value)
{
    u16 m[4] = { 0x2203, space, addr, 1 }, w;
    int i;
    for (i = 0; i < 4; i++)
        if (put(m[i]))
            return -1;
    if (get(&w, 1000) || w != 0xd301 || get(&w, 300) || get(value, 300))
        return -1;
    return 0;
}

static int output(int on)
{
    u16 v;
    if (peek(0, 0x3801, &v))
        return -1;
    return poke(0, 0x3801, on ? v | 0x0040 : v & (u16)~0x0040);
}

int sd_play(const char *name)
{
    static const u16 a[] = { 0x3301, 0x0081 }, b[] = { 0x3101, 0x0001 };
    u32 cluster = 0, c, n = 0;
    int r;
    if (slot < 0)
        return -1;
    if (playing)
        sd_play_stop();
    /* (The stock driver gives places in bytes; what it gives for a
     * card that counts in sectors has not been seen.) */
    if (high_capacity)
        return -20;
    r = find(name, &cluster, &play_size);
    if (r)
        return r;
    extents = 0;
    for (c = cluster; n < play_size; c = next_cluster(c)) {
        u32 at;
        if (last(c))
            return -4;
        at = data_start + (c - 2) * per_cluster;
        if (extents && extent[extents - 1].sector + extent[extents - 1].sectors == at) {
            extent[extents - 1].sectors += per_cluster;
        } else {
            if (extents == (int)(sizeof extent / sizeof extent[0]))
                return -21; /* in more pieces than there is room to note */
            extent[extents].sector = at;
            extent[extents++].sectors = per_cluster;
        }
        n += per_cluster * 512;
    }
    if (output(1) || say(a, 2) || say(b, 2) || poke(0, 0x0a5c, 0))
        return -22;
    if (put(0x3400) || answer(0, 0, 1000) != 0xf000)
        return -22;
    play_at = 0;
    playing = 1;
    return 0;
}

/* The next place, when the processor asks for one. */
static int feed(void)
{
    static const u16 more[] = { 0x0401, 0x0001 };
    u32 at = play_at, len, left, addr = 0;
    u16 m[8] = { 0 };
    int i;
    if (play_at >= play_size) {
        /* Nothing more: it plays out what it has and says 0xc400. */
        if (put(0x0401) || put(0x0000) || answer(0, 0, 1000) != 0xf000)
            return -23;
        return 0;
    }
    for (i = 0; i < extents; i++) {
        if (at < extent[i].sectors * 512) {
            addr = extent[i].sector * 512 + at;
            break;
        }
        at -= extent[i].sectors * 512;
    }
    if (i == extents)
        return -23;
    left = extent[i].sectors * 512 - at;
    len = play_size - play_at;
    if (len > 5000)
        len = 5000;
    if (len > left)
        len = left;
    if (say(more, 2))
        return -23;
    m[0] = (u16)(addr >> 16);
    m[1] = (u16)addr;
    m[2] = (u16)(len >> 16);
    m[3] = (u16)len;
    for (i = 0; i < 8; i++)
        if (put(m[i]))
            return -23;
    play_at += len;
    return 0;
}

int sd_play_poll(void)
{
    static const u16 done[] = { 0x3901, 0x0000 };
    u16 head, w;
    int i, r;
    if (!playing)
        return 0;
    while (inbox_in != inbox_out || !(REG8(STATUS) & (slot ? 1 : 4))) {
        if (get(&head, 100))
            break;
        if (head == 0xc701) {
            if (get(&w, 300))
                return -23;
            r = feed();
            if (r) {
                playing = 0;
                return r;
            }
        } else if (head == 0xc400 || head == 0xc500) {
            /* Played to the end; or the decoder gave up. */
            playing = 0;
            say(done, 2);
            output(0);
            return head == 0xc400 ? 0 : -24;
        } else {
            for (i = 0; i < (head & 0xff); i++)
                if (get(&w, 300))
                    break;
        }
    }
    return 1;
}

void sd_play_stop(void)
{
    u16 w;
    int i;
    if (!playing)
        return;
    playing = 0;
    /* As the stock driver stops a file in the middle.  (A request for
     * more may be on its way at that moment: whatever comes is taken
     * until the "done".) */
    if (put(0x3500))
        return;
    for (i = 0; i < 16; i++)
        if (get(&w, 500) || w == 0xf000)
            break;
    output(0);
}
