/* Formatted output and input for Doom on the RNS-E: integers, strings,
 * characters and pointers, with flags, width and precision; no floating
 * point.  scanf knows what the settings file and m_misc.c want: %d %i
 * %u %x %o %s %c %[...] with widths and %n. */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *file_rest(FILE *f, long *n);
void file_advance(FILE *f, long n);

struct out {
    char *s;
    size_t n, len;
};

static void put(struct out *o, char c)
{
    if (o->len + 1 < o->n)
        o->s[o->len] = c;
    o->len++;
}

static void pad(struct out *o, char c, int n)
{
    while (n-- > 0)
        put(o, c);
}

int vsnprintf(char *s, size_t n, const char *fmt, va_list ap)
{
    struct out o = { s, n, 0 };
    for (; *fmt; fmt++) {
        int left = 0, zero = 0, plus = 0, space = 0, alt = 0, width = 0, prec = -1, longs = 0;
        char buf[24], *p;
        const char *text;
        int len, neg = 0;
        unsigned long long v;
        unsigned base = 10;

        if (*fmt != '%') {
            put(&o, *fmt);
            continue;
        }
        for (;;) {
            char c = *++fmt;
            if (c == '-') left = 1;
            else if (c == '0') zero = 1;
            else if (c == '+') plus = 1;
            else if (c == ' ') space = 1;
            else if (c == '#') alt = 1;
            else break;
        }
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0)
                left = 1, width = -width;
            fmt++;
        } else {
            while (isdigit((unsigned char)*fmt))
                width = width * 10 + *fmt++ - '0';
        }
        if (*fmt == '.') {
            prec = 0;
            fmt++;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            } else {
                while (isdigit((unsigned char)*fmt))
                    prec = prec * 10 + *fmt++ - '0';
            }
        }
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 'j' || *fmt == 't') {
            if (*fmt == 'l')
                longs++;
            fmt++;
        }
        switch (*fmt) {
        case 'd':
        case 'i': {
            long long sv = longs > 1 ? va_arg(ap, long long) : longs ? va_arg(ap, long) : va_arg(ap, int);
            neg = sv < 0;
            v = neg ? 0ull - (unsigned long long)sv : (unsigned long long)sv;
            goto number;
        }
        case 'o':
            base = 8;
            goto unsigned_number;
        case 'x':
        case 'X':
        case 'p':
            base = 16;
            /* fall through */
        case 'u':
        unsigned_number:
            if (*fmt == 'p')
                v = (unsigned long)va_arg(ap, void *), alt = 1;
            else
                v = longs > 1 ? va_arg(ap, unsigned long long) : longs ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
        number:
            p = buf + sizeof buf;
            /* (32 bits at a time where it can: no 64-bit division
             * unless the number needs it.) */
            if (v >> 32) {
                do {
                    *--p = "0123456789abcdef"[v % base];
                    v /= base;
                } while (v);
            } else {
                unsigned w = (unsigned)v;
                do {
                    *--p = "0123456789abcdef"[w % base];
                    w /= base;
                } while (w);
            }
            if (*fmt == 'X')
                for (text = p; text < buf + sizeof buf; text++)
                    *(char *)text = (char)toupper((unsigned char)*text);
            len = (int)(buf + sizeof buf - p);
            {
                int digits = prec > len ? prec : len;
                const char *sign = neg ? "-" : plus ? "+" : space ? " " : "";
                const char *prefix = alt && base == 16 ? (*fmt == 'X' ? "0X" : "0x") : alt && base == 8 ? "0" : "";
                int total = digits + (int)strlen(sign) + (int)strlen(prefix);
                if (prec >= 0)
                    zero = 0;
                if (!left && !zero)
                    pad(&o, ' ', width - total);
                for (text = sign; *text; text++)
                    put(&o, *text);
                for (text = prefix; *text; text++)
                    put(&o, *text);
                if (!left && zero)
                    pad(&o, '0', width - total);
                pad(&o, '0', digits - len);
                while (len--)
                    put(&o, *p++);
                if (left)
                    pad(&o, ' ', width - total);
            }
            break;
        case 'c':
            buf[0] = (char)va_arg(ap, int);
            text = buf;
            len = 1;
            goto string;
        case 's':
            text = va_arg(ap, const char *);
            if (!text)
                text = "(null)";
            len = (int)(prec >= 0 ? strnlen(text, (size_t)prec) : strlen(text));
        string:
            if (!left)
                pad(&o, ' ', width - len);
            for (neg = 0; neg < len; neg++)
                put(&o, text[neg]);
            if (left)
                pad(&o, ' ', width - len);
            break;
        case 'n':
            *va_arg(ap, int *) = (int)o.len;
            break;
        case '%':
            put(&o, '%');
            break;
        case 0:
            fmt--;
            break;
        default:
            put(&o, '%');
            put(&o, *fmt);
            break;
        }
    }
    if (o.n)
        o.s[o.len < o.n ? o.len : o.n - 1] = 0;
    return (int)o.len;
}

int vsprintf(char *s, const char *fmt, va_list ap)
{
    return vsnprintf(s, (size_t)-1 >> 1, fmt, ap);
}

int snprintf(char *s, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *s, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsprintf(s, fmt, ap);
    va_end(ap);
    return r;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    char buf[512];
    va_list copy;
    int n;
    va_copy(copy, ap);
    n = vsnprintf(buf, sizeof buf, fmt, copy);
    va_end(copy);
    fwrite(buf, 1, (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1, f);
    return n;
}

int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}

/* ---- Input ------------------------------------------------------------ */

static int scan(const char *s, const char *fmt, va_list ap, int *used)
{
    const char *start = s;
    int count = 0;
    for (; *fmt; fmt++) {
        int width = 0, skip = 0;
        if (isspace((unsigned char)*fmt)) {
            while (isspace((unsigned char)*s))
                s++;
            continue;
        }
        if (*fmt != '%' || fmt[1] == '%') {
            if (*fmt == '%')
                fmt++;
            if (*s != *fmt)
                break;
            s++;
            continue;
        }
        fmt++;
        if (*fmt == '*') {
            skip = 1;
            fmt++;
        }
        while (isdigit((unsigned char)*fmt))
            width = width * 10 + *fmt++ - '0';
        while (*fmt == 'l' || *fmt == 'h')
            fmt++;
        if (*fmt == 'n') {
            if (!skip)
                *va_arg(ap, int *) = (int)(s - start);
            continue;
        }
        if (*fmt != 'c' && *fmt != '[')
            while (isspace((unsigned char)*s))
                s++;
        if (!*s)
            break;
        if (*fmt == 'd' || *fmt == 'i' || *fmt == 'u' || *fmt == 'x' || *fmt == 'X' || *fmt == 'o') {
            char num[32], *end;
            int base = *fmt == 'd' || *fmt == 'u' ? 10 : *fmt == 'i' ? 0 : *fmt == 'o' ? 8 : 16;
            unsigned long v;
            int n = (int)strlen(s);
            if (width && n > width)
                n = width;
            if (n > (int)sizeof num - 1)
                n = (int)sizeof num - 1;
            memcpy(num, s, (unsigned long)n);
            num[n] = 0;
            v = strtoul(num, &end, base);
            if (end == num)
                break;
            s += end - num;
            if (!skip) {
                *va_arg(ap, int *) = (int)v;
                count++;
            }
        } else if (*fmt == 's' || *fmt == 'c' || *fmt == '[') {
            char set[64];
            int negate = 0, n = 0, i;
            char *to = skip ? 0 : va_arg(ap, char *);
            if (*fmt == '[') {
                fmt++;
                if (*fmt == '^')
                    negate = 1, fmt++;
                for (i = 0; *fmt && (*fmt != ']' || i == 0) && i < (int)sizeof set - 1; fmt++)
                    set[i++] = *fmt;
                set[i] = 0;
            }
            if (!width)
                width = *fmt == 'c' ? 1 : 0x7fffffff;
            while (*s && n < width) {
                if (*fmt == 's' && isspace((unsigned char)*s))
                    break;
                if (*fmt == ']' && (strchr(set, *s) != 0) == negate)
                    break;
                if (to)
                    to[n] = *s;
                n++;
                s++;
            }
            if (!n)
                break;
            if (to && *fmt != 'c')
                to[n] = 0;
            if (to)
                count++;
        } else {
            break;
        }
    }
    *used = (int)(s - start);
    return count;
}

int vsscanf(const char *s, const char *fmt, va_list ap)
{
    int used;
    return scan(s, fmt, ap, &used);
}

int sscanf(const char *s, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsscanf(s, fmt, ap);
    va_end(ap);
    return r;
}

/* The settings file is in RAM (libc.c): scanned in place.  It ends in a
 * NUL there only by luck, so the rest of it is copied out a line or so
 * at a time. */
int fscanf(FILE *f, const char *fmt, ...)
{
    char buf[512];
    long n;
    const char *rest = file_rest(f, &n);
    va_list ap;
    int r, used;
    if (n <= 0)
        return EOF;
    if (n > (long)sizeof buf - 1)
        n = (long)sizeof buf - 1;
    memcpy(buf, rest, (unsigned long)n);
    buf[n] = 0;
    va_start(ap, fmt);
    r = scan(buf, fmt, ap, &used);
    va_end(ap);
    file_advance(f, used);
    return r;
}
