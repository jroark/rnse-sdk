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

/* The compiler may call these on its own. */
void *memset(void *d, int c, unsigned long n)
{
    u8 *p = d;
    while (n--)
        *p++ = (u8)c;
    return d;
}

void *memcpy(void *d, const void *s, unsigned long n)
{
    u8 *p = d;
    const u8 *q = s;
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

void unexpected(u32 expevt, u32 intevt, u32 spc)
{
    console_puts("\nunexpected exception: EXPEVT=");
    console_hex(expevt);
    console_puts(" INTEVT=");
    console_hex(intevt);
    console_puts(" SPC=");
    console_hex(spc);
    console_puts("\n");
}

void screen_fill(int x, int y, int w, int h, u8 colour)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            screen_pixel(x + i, y + j, colour);
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
    { '!', G(00100, 00100, 00100, 00100, 00100, 00000, 00100) },
    { '/', G(00001, 00001, 00010, 00100, 01000, 10000, 10000) },
    { ',', G(00000, 00000, 00000, 00000, 01100, 00100, 01000) },
    { '=', G(00000, 00000, 11111, 00000, 11111, 00000, 00000) },
};

static const u8 *glyph(char c)
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
        const u8 *g = glyph(*s);
        int row, col;
        if (!g)
            continue;
        for (row = 0; row < 7; row++)
            for (col = 0; col < 5; col++)
                if (g[row] & (0x10 >> col))
                    screen_fill(x + col * scale, y + row * scale, scale, scale, colour);
    }
}
