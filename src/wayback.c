/* The way back: ending a program by starting the stock loader.
 *
 * A unit runs a program in the loader's place because its installed
 * software took the disc for an update, and it will do so again at
 * every start for as long as the disc is in the drive: the installed
 * software does not get as far as ejecting it.  The stock loader is
 * what ends that.  It updates the unit from the disc, which leaves the
 * unit with the disc's software index, and from then on the disc is no
 * update any more.
 *
 * `rnse custom` puts the stock loader into the image, packed, above
 * the memory the program uses.  stock_update unpacks it elsewhere in
 * RAM, checks it, and has a few instructions outside the program's
 * place copy it over the program and call it as the installed software
 * would have, with the arguments the program itself was started with.
 * Nothing but RAM and the CPU is needed for that.
 *
 * The packed loader is in the image twice, block by block, each block
 * with a sum of its own, and so is its directory (fw.rs, way_back).
 * The place a loader is copied to is not the loader's alone: one stock
 * operating system has been seen to keep variables in the middle of it
 * and to change them until the program has taken over.  A block that
 * got damaged that way is taken from its other copy. */
#include "rnse.h"

extern const u32 way_back_offset[2];
extern const char _load[];
extern const u32 way_back_stub[], way_back_stub_end[];
/* From the hardware layer: the port with the watchdog's strobe in bit 0
 * and a port whose bits 3 and 2 can be watched from outside, or 0. */
extern const u32 way_back_strobe, way_back_marker;
void irq_disable(void);

u32 start_args[3];
u32 start_state[4];

#define UNCACHED(a) (((u32)(a) & 0x1fffffff) | 0xa0000000)

/* A directory that is whole: "RNSEBACK", length, block size, number of
 * blocks, the sum of the long words that follow, then for either copy
 * and each block: offset from the load address, packed length, sum of
 * the unpacked bytes. */
static const u32 *directory(void)
{
    int k;
    for (k = 0; k < 2; k++) {
        const u32 *dir = (const u32 *)(_load + way_back_offset[k]);
        u32 n, i, sum = 0;
        if (!way_back_offset[k] || dir[0] != 0x524e5345 || dir[1] != 0x4241434b)
            continue;
        n = dir[4];
        if (n > 4096)
            continue;
        for (i = 0; i < n * 6; i++)
            sum += dir[6 + i];
        if (sum == dir[5])
            return dir;
    }
    return 0;
}

int stock_loader_carried(void)
{
    return directory() != 0;
}

/* LZSS as `rnse custom` packs it (fw.rs, lzss_pack). */
static u32 unpack(const u8 *in, u32 n, u8 *out, u32 max)
{
    u32 i = 0, o = 0;
    while (i < n) {
        u32 flags = in[i++], k;
        for (k = 0; k < 8 && i < n; k++, flags >>= 1) {
            if (flags & 1) {
                if (o >= max)
                    return 0;
                out[o++] = in[i++];
            } else {
                u32 b0 = in[i], b1 = in[i + 1];
                u32 back = b0 | (b1 >> 4) << 8, len = (b1 & 15) + 3;
                i += 2;
                if (!back || back > o || o + len > max)
                    return 0;
                while (len--) {
                    out[o] = out[o - back];
                    o++;
                }
            }
        }
    }
    return o;
}

int stock_update(void)
{
    const u32 *dir = directory();
    u8 *loader = (u8 *)(way_back_scratch + 0x1000);
    volatile u32 *stub = (volatile u32 *)UNCACHED(way_back_scratch);
    volatile u32 *params = (volatile u32 *)UNCACHED(way_back_scratch + 0x800);
    u32 length, block, n, b, i, words;

    if (!dir)
        return -1;
    length = dir[2];
    block = dir[3];
    n = dir[4];
    for (b = 0; b < n; b++) {
        u8 *out = loader + b * block;
        u32 want = length - b * block, copy;
        if (want > block)
            want = block;
        for (copy = 0; copy < 2; copy++) {
            const u32 *e = dir + 6 + (copy * n + b) * 3;
            u32 sum = 0;
            if (unpack((const u8 *)_load + e[0], e[1], out, want) != want)
                continue;
            for (i = 0; i < want; i++)
                sum += out[i];
            if (sum == e[2])
                break;
        }
        if (copy == 2)
            return -2; /* both copies of a block are damaged */
    }

    /* From here on there is no going on with the program. */
    display_halt();
    irq_disable();
    words = (u32)(way_back_stub_end - way_back_stub);
    for (i = 0; i < words; i++)
        stub[i] = way_back_stub[i];
    params[0] = (u32)_load;
    params[1] = (length + 31) >> 5;
    /* The entry points follow the 0x80 bytes of the module's header. */
    params[2] = ((const u32 *)loader)[0x80 / 4];
    params[3] = ((const u32 *)loader)[0x88 / 4];
    params[4] = start_args[0];
    params[5] = start_args[1];
    params[6] = start_args[2];
    params[7] = way_back_scratch - 16;
    params[8] = way_back_strobe;
    params[9] = way_back_marker;
    params[10] = way_back_marker ? (u32)(REG8(way_back_marker) | 0x0c) : 0;
    ((void (*)(u32, u32, u32, u32))(u32)stub)((u32)loader, UNCACHED(_load), (length + 3) >> 2, (u32)params);
    return -3; /* not reached */
}

/* Another program in this one's place: `image` is what build.sh makes
 * (out/<example>-<unit>.bin, the image from the module's entry points
 * on), `bytes` long, somewhere in RAM outside the program's place.  It
 * is copied over this program and started as this one was, with the
 * same arguments, by the few instructions that also start the stock
 * loader; and it is told where the packed stock loader is, which stays
 * where `rnse custom` put it, so that its stock_update works as this
 * one's would.  Returns only when the image cannot be one. */
int run_image(void *image, u32 bytes)
{
    volatile u32 *stub = (volatile u32 *)UNCACHED(way_back_scratch);
    volatile u32 *params = (volatile u32 *)UNCACHED(way_back_scratch + 0x800);
    u32 *words = image;
    u32 i, n;

    if (bytes < 0x20 || (words[0] & 0xfff00000) != ((u32)_load & 0xfff00000))
        return -1; /* its entry point is not in the loader's place */
    words[6] = way_back_offset[0];
    words[7] = way_back_offset[1];
    display_halt();
    irq_disable();
    n = (u32)(way_back_stub_end - way_back_stub);
    for (i = 0; i < n; i++)
        stub[i] = way_back_stub[i];
    params[0] = (u32)_load;
    params[1] = (0x80 + bytes + 31) >> 5;
    params[2] = words[0];
    params[3] = words[2];
    params[4] = start_args[0];
    params[5] = start_args[1];
    params[6] = start_args[2];
    params[7] = way_back_scratch - 16;
    params[8] = way_back_strobe;
    params[9] = way_back_marker;
    params[10] = way_back_marker ? (u32)(REG8(way_back_marker) | 0x0c) : 0;
    ((void (*)(u32, u32, u32, u32))(u32)stub)((u32)image, UNCACHED(_load) + 0x80, (bytes + 3) >> 2, (u32)params);
    return -3; /* not reached */
}

/* The upper 256 KiB of way_back_scratch is owned by a cooperative
 * session, even while a payload runs. All control data is uncached. */
#define SESSION ((volatile u32 *)0xac7c0000)
#define SNAPSHOT 0xac7e0000u
#define SESSION_MAGIC 0x53444c31u
#define LOADER_BASE 0x8df00000u
#define LOADER_SIZE 0x20000u

static int session_valid(void)
{
    return (u32)_load == LOADER_BASE && SESSION[0] == SESSION_MAGIC &&
           SESSION[1] == ~SESSION_MAGIC && SESSION[2] == LOADER_BASE && SESSION[3] < 2;
}

int loader_returned(int *status, u32 *blocked)
{
    int slot;
    if (!session_valid() || SESSION[4] != 2)
        return -1;
    slot = (int)SESSION[3];
    *status = (int)SESSION[5];
    *blocked = SESSION[10] & 3;
    SESSION[0] = 0;
    return slot;
}

int loader_run(void *image, u32 bytes, int slot, u32 blocked)
{
    const u32 *w = image;
    u32 i, sum = 0;
    volatile u32 *save = (volatile u32 *)SNAPSHOT;
    if ((u32)_load != LOADER_BASE || slot < 0 || slot > 1 || bytes < 32 ||
        bytes > 0x1c000 || w[4] != 0x524e5345 || w[5] != LOADER_SIZE)
        return -1;
    for (i = 0; i < 3; i++)
        if ((w[i] & 1) || w[i] < LOADER_BASE + 0xa0 || w[i] >= LOADER_BASE + 0x80 + bytes)
            return -2;
    display_halt();
    irq_disable();
    for (i = 0; i < LOADER_SIZE; i += 32) {
        u32 a = LOADER_BASE + i;
        __asm__ volatile ("ocbp @%0" : : "r"(a) : "memory");
    }
    for (i = 0; i < LOADER_SIZE / 4; i++) {
        u32 v = ((volatile u32 *)UNCACHED(LOADER_BASE))[i];
        save[i] = v;
        sum += v;
        if (!(i & 1023)) REG8(0xb2300001) ^= 1;
    }
    SESSION[0] = SESSION_MAGIC;
    SESSION[1] = ~SESSION_MAGIC;
    SESSION[2] = LOADER_BASE;
    SESSION[3] = (u32)slot;
    SESSION[4] = 1;
    SESSION[6] = sum;
    SESSION[10] = blocked & 3;
    for (i = 0; i < 3; i++) SESSION[7 + i] = start_args[i];
    return run_image(image, bytes);
}

void loader_return(int status)
{
    volatile u32 *stub = (volatile u32 *)0xac7c1000;
    volatile u32 *params = (volatile u32 *)0xac7c1800;
    u32 i, sum = 0;
    if (!session_valid() || SESSION[4] != 1)
        return;
    for (i = 0; i < LOADER_SIZE / 4; i++)
        sum += ((volatile u32 *)SNAPSHOT)[i];
    if (sum != SESSION[6])
        return; /* corrupted backup: never execute it */
    display_halt();
    irq_disable();
    /* Stop timers and every DMA channel before restoring live code. */
    REG8(0xffd80004) = 0;
    for (i = 0; i < 8; i++)
        REG32(0xffa0000c + (i < 4 ? i : i + 1) * 0x10) = 0;
    SESSION[4] = 2;
    SESSION[5] = (u32)status;
    for (i = 0; i < (u32)(way_back_stub_end - way_back_stub); i++)
        stub[i] = way_back_stub[i];
    params[0] = LOADER_BASE;
    params[1] = LOADER_SIZE / 32;
    params[2] = ((volatile u32 *)SNAPSHOT)[0x80 / 4];
    params[3] = params[2];
    for (i = 0; i < 3; i++) params[4 + i] = SESSION[7 + i];
    params[7] = 0xac7dfff0;
    params[8] = way_back_strobe;
    params[9] = 0;
    params[10] = 0;
    ((void (*)(u32, u32, u32, u32))(u32)stub)(SNAPSHOT, UNCACHED(LOADER_BASE), LOADER_SIZE / 4, (u32)params);
}
