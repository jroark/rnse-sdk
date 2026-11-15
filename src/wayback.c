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
    words[5] = way_back_offset[0];
    words[6] = way_back_offset[1];
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
