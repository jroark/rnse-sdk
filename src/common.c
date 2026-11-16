/* What both units share: text output helpers, the font, division. */
#include "rnse.h"

u32 udiv(u32 a, u32 b)
{
    u32 q = 0, bit = 1;
    if (b == 0)
        return 0xffffffff;
    while (b < a && !(b & 0x80000000)) {
        b <<= 1;
        bit <<= 1;
    }
    while (bit) {
        if (a >= b) {
            a -= b;
            q |= bit;
        }
        b >>= 1;
        bit >>= 1;
    }
    return q;
}

u32 umod(u32 a, u32 b)
{
    return a - udiv(a, b) * b;
}

/* The compiler may call these on its own, and code from elsewhere
 * (third_party) leans on them: a long word at a time where the
 * addresses allow, eight to a round.  (The attribute keeps GCC from
 * turning the loops back into calls of memset and memcpy.) */
#define NO_LIBCALLS __attribute__((optimize("no-tree-loop-distribute-patterns")))

NO_LIBCALLS void *memset(void *d, int c, unsigned long n)
{
    u8 *p = d;
    while (n && ((u32)p & 3)) {
        *p++ = (u8)c;
        n--;
    }
    if (n >= 4) {
        u32 w = (u8)c * 0x01010101u, *q = (u32 *)p;
        for (; n >= 32; n -= 32, q += 8) {
            q[0] = w, q[1] = w, q[2] = w, q[3] = w;
            q[4] = w, q[5] = w, q[6] = w, q[7] = w;
        }
        for (; n >= 4; n -= 4)
            *q++ = w;
        p = (u8 *)q;
    }
    while (n--)
        *p++ = (u8)c;
    return d;
}

NO_LIBCALLS void *memcpy(void *d, const void *s, unsigned long n)
{
    u8 *p = d;
    const u8 *q = s;
    if ((((u32)p ^ (u32)q) & 3) == 0) {
        while (n && ((u32)p & 3)) {
            *p++ = *q++;
            n--;
        }
        {
            u32 *pw = (u32 *)p;
            const u32 *qw = (const u32 *)q;
            for (; n >= 32; n -= 32, pw += 8, qw += 8) {
                u32 a = qw[0], b = qw[1], c = qw[2], e = qw[3];
                pw[0] = a, pw[1] = b, pw[2] = c, pw[3] = e;
                a = qw[4], b = qw[5], c = qw[6], e = qw[7];
                pw[4] = a, pw[5] = b, pw[6] = c, pw[7] = e;
            }
            for (; n >= 4; n -= 4)
                *pw++ = *qw++;
            p = (u8 *)pw;
            q = (const u8 *)qw;
        }
    }
    while (n--)
        *p++ = *q++;
    return d;
}

void console_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            console_putc('\r');
        console_putc(*s++);
    }
}

void console_hex(u32 v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        console_putc("0123456789abcdef"[(v >> i) & 15]);
}

void console_dec(u32 v)
{
    char buf[11];
    int n = 0;
    do {
        buf[n++] = (char)('0' + umod(v, 10));
        v = udiv(v, 10);
    } while (v);
    while (n)
        console_putc(buf[--n]);
}

void delay_ms(u32 ms)
{
    u32 until = millis() + ms;
    /* Asleep between ticks rather than spinning. */
    while ((s32)(millis() - until) < 0)
        cpu_idle();
}

static char *hex8(char *p, u32 v)
{
    int i;
    for (i = 28; i >= 0; i -= 4)
        *p++ = "0123456789ABCDEF"[(v >> i) & 15];
    return p;
}

void unexpected(u32 expevt, u32 intevt, u32 spc)
{
    char text[40], *p;
    const char *k;
    console_puts("\nunexpected exception: EXPEVT=");
    console_hex(expevt);
    console_puts(" INTEVT=");
    console_hex(intevt);
    console_puts(" SPC=");
    console_hex(spc);
    console_puts("\n");
    /* On the screen as well, since a unit has nothing on its debug port:
     * what happened and where.  Only for as long as the unit stays up:
     * the caller then waits with interrupts blocked, nothing serves the
     * watchdog, and the unit resets into its own software, which can
     * open the display and eject the disc.  Serving the watchdog from
     * here to keep the screen up would leave eject and the display key
     * dead for as long as it does (CLAUDE.md, "Mandatory hardware
     * behavior"). */
    screen_palette(0, 0, 0, 0);
    screen_palette(1, 255, 255, 255);
    screen_fill(0, 0, screen_width, 44, 0);
    screen_text(4, 4, "UNEXPECTED EXCEPTION", 1, 2);
    p = text;
    for (k = "EXPEVT "; *k; k++)
        *p++ = *k;
    p = hex8(p, expevt);
    for (k = " PC "; *k; k++)
        *p++ = *k;
    p = hex8(p, spc);
    *p = 0;
    screen_text(4, 26, text, 1, 2);
}

void screen_fill(int x, int y, int w, int h, u8 colour)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            screen_pixel(x + i, y + j, colour);
}

/* A picture of w x h pixels, a row every `pitch` bytes, at (x, y), a
 * pixel at a time (the 192's hardware layer has a faster one). */
__attribute__((weak)) void screen_blit(int x, int y, int w, int h, const u8 *pixels, int pitch)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            screen_pixel(x + i, y + j, pixels[j * pitch + i]);
}

/* 5x7 font: seven rows a glyph, bit 4 the left column. */
#define G(a, b, c, d, e, f, g) { 0b##a, 0b##b, 0b##c, 0b##d, 0b##e, 0b##f, 0b##g }
static const u8 digits[10][7] = {
    G(01110, 10001, 10011, 10101, 11001, 10001, 01110),
    G(00100, 01100, 00100, 00100, 00100, 00100, 01110),
    G(01110, 10001, 00001, 00010, 00100, 01000, 11111),
    G(11110, 00001, 00001, 01110, 00001, 00001, 11110),
    G(00010, 00110, 01010, 10010, 11111, 00010, 00010),
    G(11111, 10000, 11110, 00001, 00001, 10001, 01110),
    G(00110, 01000, 10000, 11110, 10001, 10001, 01110),
    G(11111, 00001, 00010, 00100, 01000, 01000, 01000),
    G(01110, 10001, 10001, 01110, 10001, 10001, 01110),
    G(01110, 10001, 10001, 01111, 00001, 00010, 01100),
};
static const u8 letters[26][7] = {
    G(01110, 10001, 10001, 11111, 10001, 10001, 10001),
    G(11110, 10001, 10001, 11110, 10001, 10001, 11110),
    G(01110, 10001, 10000, 10000, 10000, 10001, 01110),
    G(11110, 10001, 10001, 10001, 10001, 10001, 11110),
    G(11111, 10000, 10000, 11110, 10000, 10000, 11111),
    G(11111, 10000, 10000, 11110, 10000, 10000, 10000),
    G(01110, 10001, 10000, 10111, 10001, 10001, 01111),
    G(10001, 10001, 10001, 11111, 10001, 10001, 10001),
    G(01110, 00100, 00100, 00100, 00100, 00100, 01110),
    G(00111, 00010, 00010, 00010, 00010, 10010, 01100),
    G(10001, 10010, 10100, 11000, 10100, 10010, 10001),
    G(10000, 10000, 10000, 10000, 10000, 10000, 11111),
    G(10001, 11011, 10101, 10101, 10001, 10001, 10001),
    G(10001, 11001, 10101, 10011, 10001, 10001, 10001),
    G(01110, 10001, 10001, 10001, 10001, 10001, 01110),
    G(11110, 10001, 10001, 11110, 10000, 10000, 10000),
    G(01110, 10001, 10001, 10001, 10101, 10010, 01101),
    G(11110, 10001, 10001, 11110, 10100, 10010, 10001),
    G(01111, 10000, 10000, 01110, 00001, 00001, 11110),
    G(11111, 00100, 00100, 00100, 00100, 00100, 00100),
    G(10001, 10001, 10001, 10001, 10001, 10001, 01110),
    G(10001, 10001, 10001, 10001, 10001, 01010, 00100),
    G(10001, 10001, 10001, 10101, 10101, 11011, 10001),
    G(10001, 10001, 01010, 00100, 01010, 10001, 10001),
    G(10001, 10001, 01010, 00100, 00100, 00100, 00100),
    G(11111, 00001, 00010, 00100, 01000, 10000, 11111),
};
static const struct {
    char c;
    u8 rows[7];
} marks[] = {
    { '.', G(00000, 00000, 00000, 00000, 00000, 01100, 01100) },
    { ':', G(00000, 01100, 01100, 00000, 01100, 01100, 00000) },
    { '-', G(00000, 00000, 00000, 11111, 00000, 00000, 00000) },
    { '_', G(00000, 00000, 00000, 00000, 00000, 00000, 11111) },
    { '!', G(00100, 00100, 00100, 00100, 00100, 00000, 00100) },
    { '/', G(00001, 00001, 00010, 00100, 01000, 10000, 10000) },
    { ',', G(00000, 00000, 00000, 00000, 01100, 00100, 01000) },
    { '=', G(00000, 00000, 11111, 00000, 11111, 00000, 00000) },
};

const u8 *font_glyph(char c)
{
    unsigned i;
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    if (c >= '0' && c <= '9')
        return digits[c - '0'];
    if (c >= 'A' && c <= 'Z')
        return letters[c - 'A'];
    for (i = 0; i < sizeof marks / sizeof marks[0]; i++)
        if (marks[i].c == c)
            return marks[i].rows;
    return 0;
}

void screen_text(int x, int y, const char *s, u8 colour, int scale)
{
    for (; *s; s++, x += 6 * scale) {
        const u8 *g = font_glyph(*s);
        int row, col;
        if (!g)
            continue;
        for (row = 0; row < 7; row++)
            for (col = 0; col < 5; col++)
                if (g[row] & (0x10 >> col))
                    screen_fill(x + col * scale, y + row * scale, scale, scale, colour);
    }
}
