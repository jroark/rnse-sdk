/* The C library of bare-metal programs on the RNS-E (Doom, umac): strings, numbers, memory, files.
 * (memset and memcpy are the SDK's, sdk/src/common.c; formatted output
 * and input are in printf.c.) */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "rnse.h"
#include "rnse_libc.h"

int errno;

/* ---- Strings ---------------------------------------------------------- */

void *memmove(void *to, const void *from, size_t n)
{
    unsigned char *d = to;
    const unsigned char *s = from;
    if (d <= s || d >= s + n)
        return memcpy(to, from, n);
    while (n--)
        d[n] = s[n];
    return to;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    for (; n; n--, p++, q++)
        if (*p != *q)
            return *p - *q;
    return 0;
}

void *memchr(const void *p, int c, size_t n)
{
    const unsigned char *s = p;
    for (; n; n--, s++)
        if (*s == (unsigned char)c)
            return (void *)s;
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}

size_t strnlen(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i])
        i++;
    return i;
}

char *strcpy(char *to, const char *from)
{
    char *d = to;
    while ((*d++ = *from++))
        ;
    return to;
}

char *strncpy(char *to, const char *from, size_t n)
{
    size_t i = 0;
    for (; i < n && from[i]; i++)
        to[i] = from[i];
    for (; i < n; i++)
        to[i] = 0;
    return to;
}

char *strcat(char *to, const char *from)
{
    strcpy(to + strlen(to), from);
    return to;
}

char *strncat(char *to, const char *from, size_t n)
{
    char *d = to + strlen(to);
    while (n-- && *from)
        *d++ = *from++;
    *d = 0;
    return to;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++)
        if (*a != *b || !*a)
            return (unsigned char)*a - (unsigned char)*b;
    return 0;
}

int strcasecmp(const char *a, const char *b)
{
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b))
        a++, b++;
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y || !x)
            return x - y;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return 0;
    }
}

char *strrchr(const char *s, int c)
{
    const char *last = 0;
    for (;; s++) {
        if (*s == (char)c)
            last = s;
        if (!*s)
            return (char *)last;
    }
}

char *strstr(const char *s, const char *find)
{
    size_t n = strlen(find);
    for (; *s; s++)
        if (!strncmp(s, find, n))
            return (char *)s;
    return n ? 0 : (char *)s;
}

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

size_t strspn(const char *s, const char *accept)
{
    size_t i = 0;
    while (s[i] && strchr(accept, s[i]))
        i++;
    return i;
}

size_t strcspn(const char *s, const char *reject)
{
    size_t i = 0;
    while (s[i] && !strchr(reject, s[i]))
        i++;
    return i;
}

char *strerror(int e)
{
    switch (e) {
    case ENOENT: return "no such file";
    case ENOMEM: return "out of memory";
    case ENOSPC: return "no room";
    default: return "error";
    }
}

/* ---- Numbers ---------------------------------------------------------- */

unsigned long strtoul(const char *s, char **end, int base)
{
    unsigned long v = 0;
    const char *p = s;
    int neg = 0, any = 0;
    while (isspace((unsigned char)*p))
        p++;
    if (*p == '+' || *p == '-')
        neg = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] | 32) == 'x' && isxdigit((unsigned char)p[2])) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = *p == '0' ? 8 : 10;
    }
    for (;; p++) {
        int d;
        if (isdigit((unsigned char)*p))
            d = *p - '0';
        else if (isalpha((unsigned char)*p))
            d = (*p | 32) - 'a' + 10;
        else
            break;
        if (d >= base)
            break;
        v = v * (unsigned)base + (unsigned)d;
        any = 1;
    }
    if (end)
        *end = (char *)(any ? p : s);
    return neg ? -v : v;
}

long strtol(const char *s, char **end, int base)
{
    return (long)strtoul(s, end, base);
}

int atoi(const char *s)
{
    return (int)strtol(s, 0, 10);
}

long atol(const char *s)
{
    return strtol(s, 0, 10);
}

int abs(int v)
{
    return v < 0 ? -v : v;
}

long labs(long v)
{
    return v < 0 ? -v : v;
}

/* Shell sort: few elements, and no recursion. */
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    unsigned char *a = base, t;
    size_t gap, i, j, k;
    for (gap = n / 2; gap; gap /= 2)
        for (i = gap; i < n; i++)
            for (j = i; j >= gap && cmp(a + (j - gap) * size, a + j * size) > 0; j -= gap)
                for (k = 0; k < size; k++) {
                    t = a[(j - gap) * size + k];
                    a[(j - gap) * size + k] = a[j * size + k];
                    a[j * size + k] = t;
                }
}

/* ---- The rest of a system -------------------------------------------- */

char *getenv(const char *name)
{
    (void)name;
    return 0;
}

int system(const char *command)
{
    (void)command;
    return -1;
}

int isatty(int fd)
{
    (void)fd;
    return 1; /* the "console" is the serial port: no dialog wanted */
}

int usleep(unsigned int us)
{
    u32 t = micros();
    while (micros() - t < us)
        port_idle();
    return 0;
}

time_t time(time_t *t)
{
    time_t v = (time_t)(millis() / 1000);
    if (t)
        *t = v;
    return v;
}

int mkdir(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    return 0;
}

int atexit(void (*f)(void))
{
    (void)f;
    return 0;
}

void exit(int status)
{
    port_exit(status);
}

void abort(void)
{
    port_exit(-1);
}

void rnse_assert(const char *what, const char *file, int line)
{
    fprintf(stderr, "assertion failed: %s (%s:%d)\n", what, file, line);
    port_exit(-1);
}

/* ---- Memory: first fit over the heap of handoff.h, blocks merged when
 * freed.  Each block has a header of 32 bytes (its size and whether it
 * is free), so that what is handed out is aligned to the cache line. */

struct block {
    u32 size; /* including this header */
    u32 free;
    struct block *next;
    u32 pad[5];
};

static struct block *first;

void heap_init(void *base, u32 size)
{
    first = (struct block *)(((u32)base + 31) & ~31u);
    first->size = (size - ((u32)first - (u32)base)) & ~31u;
    first->free = 1;
    first->next = 0;
}

u32 heap_left(void)
{
    struct block *b;
    u32 n = 0;
    for (b = first; b; b = b->next)
        if (b->free)
            n += b->size - sizeof *b;
    return n;
}

void *malloc(unsigned long n)
{
    struct block *b;
    u32 want = ((u32)n + sizeof(struct block) + 31) & ~31u;
    for (b = first; b; b = b->next) {
        if (!b->free || b->size < want)
            continue;
        if (b->size - want >= 2 * sizeof(struct block)) {
            struct block *rest = (struct block *)((u8 *)b + want);
            rest->size = b->size - want;
            rest->free = 1;
            rest->next = b->next;
            b->next = rest;
            b->size = want;
        }
        b->free = 0;
        return b + 1;
    }
    errno = ENOMEM;
    return 0;
}

void free(void *p)
{
    struct block *b, *prev = 0;
    if (!p)
        return;
    for (b = first; b && b + 1 != p; b = b->next)
        prev = b;
    if (!b)
        return;
    b->free = 1;
    if (b->next && b->next->free) {
        b->size += b->next->size;
        b->next = b->next->next;
    }
    if (prev && prev->free) {
        prev->size += b->size;
        prev->next = b->next;
    }
}

void *calloc(size_t n, size_t size)
{
    void *p = malloc(n * size);
    if (p)
        memset(p, 0, n * size);
    return p;
}

void *realloc(void *p, size_t n)
{
    struct block *b = (struct block *)p - 1;
    void *q;
    if (!p)
        return malloc(n);
    if (b->size - sizeof *b >= n)
        return p;
    q = malloc(n);
    if (q) {
        memcpy(q, p, b->size - sizeof *b);
        free(p);
    }
    return q;
}

/* ---- Files ------------------------------------------------------------ *
 *
 * The WAD in RAM, read only, and up to FILES more, kept in RAM until
 * the unit is switched off: the settings (default.cfg) and saved games.
 * Each grows as it is written.  stdout and stderr go to the serial
 * port; stderr is also kept for the screen an error ends on. */

#define FILES 16
#define NAME 48

struct rnse_file {
    int kind; /* 0 closed, 1 file, 2 stdout, 3 stderr */
    int store;
    long at;
    int writable, eof;
};

static struct {
    char name[NAME];
    u8 *data;
    long size, room;
    int used, fixed;
    /* For a file that is not in RAM (Quake's PAK0.PAK, on the card):
     * where its bytes come from. */
    long (*read_at)(long at, void *buf, long n);
} store[FILES];

static FILE streams[FILES + 3] = { { .kind = 0 }, { .kind = 2 }, { .kind = 3 } };
FILE *stdin = &streams[0], *stdout = &streams[1], *stderr = &streams[2];

char error_text[1024];
static int error_len;

/* A file in RAM (Doom's WAD), put in place before anything opens it. */
void files_add_fixed(const char *name, void *data, long size)
{
    int i;
    for (i = 0; i < FILES; i++)
        if (!store[i].used) {
            strncpy(store[i].name, name, NAME - 1);
            store[i].data = data;
            store[i].size = size;
            store[i].room = size;
            store[i].used = 1;
            store[i].fixed = 1;
            return;
        }
}

/* A file read through a function of the program's (read only). */
void files_add_reader(const char *name, long size, long (*read_at)(long at, void *buf, long n))
{
    int i;
    for (i = 0; i < FILES; i++)
        if (!store[i].used) {
            memset(&store[i], 0, sizeof store[i]);
            strncpy(store[i].name, name, NAME - 1);
            store[i].size = size;
            store[i].room = size;
            store[i].used = 1;
            store[i].fixed = 1;
            store[i].read_at = read_at;
            return;
        }
}

static const char *base_name(const char *path)
{
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

static int find_store(const char *path)
{
    int i;
    path = base_name(path);
    for (i = 0; i < FILES; i++)
        if (store[i].used && !strcasecmp(store[i].name, path))
            return i;
    return -1;
}

FILE *fopen(const char *path, const char *mode)
{
    int s = find_store(path), i, write = mode[0] == 'w' || mode[0] == 'a';
    FILE *f = 0;
    for (i = 3; i < FILES + 3; i++)
        if (!streams[i].kind) {
            f = &streams[i];
            break;
        }
    if (!f) {
        errno = ENOMEM;
        return 0;
    }
    if (s < 0) {
        if (!write) {
            errno = ENOENT;
            return 0;
        }
        for (s = 0; s < FILES && store[s].used; s++)
            ;
        if (s == FILES) {
            errno = ENOSPC;
            return 0;
        }
        memset(&store[s], 0, sizeof store[s]);
        strncpy(store[s].name, base_name(path), NAME - 1);
        store[s].used = 1;
    } else if (write && store[s].fixed) {
        errno = EINVAL;
        return 0;
    }
    if (mode[0] == 'w')
        store[s].size = 0;
    f->kind = 1;
    f->store = s;
    f->at = mode[0] == 'a' ? store[s].size : 0;
    f->writable = write || strchr(mode, '+') != 0;
    f->eof = 0;
    return f;
}

int fclose(FILE *f)
{
    if (f && f->kind == 1)
        f->kind = 0;
    return 0;
}

size_t fread(void *buf, size_t size, size_t n, FILE *f)
{
    long left, want = (long)(size * n);
    if (!f || f->kind != 1 || !size)
        return 0;
    left = store[f->store].size - f->at;
    if (want > left) {
        want = left < 0 ? 0 : left;
        f->eof = 1;
    }
    if (store[f->store].read_at) {
        long got = want ? store[f->store].read_at(f->at, buf, want) : 0;
        if (got < want) {
            f->eof = 1;
            want = got < 0 ? 0 : got;
        }
    } else {
        memcpy(buf, store[f->store].data + f->at, (unsigned long)want);
    }
    f->at += want;
    return (size_t)want / size;
}

static void to_console(const char *p, size_t n, int keep)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (p[i] == '\n')
            console_putc('\r');
        console_putc(p[i]);
        if (keep && error_len < (int)sizeof error_text - 1)
            error_text[error_len++] = p[i];
    }
}

size_t fwrite(const void *buf, size_t size, size_t n, FILE *f)
{
    long want = (long)(size * n), end;
    if (!f || !size)
        return 0;
    if (f->kind == 2 || f->kind == 3) {
        to_console(buf, (size_t)want, f->kind == 3);
        return n;
    }
    if (f->kind != 1 || !f->writable)
        return 0;
    end = f->at + want;
    if (end > store[f->store].room) {
        long room = (end + 4095) & ~4095L;
        u8 *p = realloc(store[f->store].data, (size_t)room);
        if (!p) {
            errno = ENOSPC;
            return 0;
        }
        store[f->store].data = p;
        store[f->store].room = room;
    }
    if (f->at > store[f->store].size)
        memset(store[f->store].data + store[f->store].size, 0,
               (unsigned long)(f->at - store[f->store].size));
    memcpy(store[f->store].data + f->at, buf, (unsigned long)want);
    f->at = end;
    if (end > store[f->store].size)
        store[f->store].size = end;
    return n;
}

int fseek(FILE *f, long offset, int whence)
{
    long base = 0;
    if (!f || f->kind != 1)
        return -1;
    if (whence == SEEK_CUR)
        base = f->at;
    else if (whence == SEEK_END)
        base = store[f->store].size;
    if (base + offset < 0)
        return -1;
    f->at = base + offset;
    f->eof = 0;
    return 0;
}

long ftell(FILE *f)
{
    return f && f->kind == 1 ? f->at : -1;
}

int fflush(FILE *f)
{
    (void)f;
    return 0;
}

int feof(FILE *f)
{
    return f && f->eof;
}

int ferror(FILE *f)
{
    (void)f;
    return 0;
}

int fgetc(FILE *f)
{
    unsigned char c;
    return fread(&c, 1, 1, f) == 1 ? c : EOF;
}

char *fgets(char *s, int n, FILE *f)
{
    int i = 0, c = 0;
    while (i < n - 1 && (c = fgetc(f)) != EOF) {
        s[i++] = (char)c;
        if (c == '\n')
            break;
    }
    if (!i)
        return 0;
    s[i] = 0;
    return s;
}

int fputc(int c, FILE *f)
{
    char b = (char)c;
    return fwrite(&b, 1, 1, f) == 1 ? (unsigned char)c : EOF;
}

int fputs(const char *s, FILE *f)
{
    size_t n = strlen(s);
    return fwrite(s, 1, n, f) == n ? 0 : EOF;
}

int puts(const char *s)
{
    fputs(s, stdout);
    return fputc('\n', stdout);
}

int putchar(int c)
{
    return fputc(c, stdout);
}

int fileno(FILE *f)
{
    return (int)(f - streams);
}

void setbuf(FILE *f, char *buf)
{
    (void)f;
    (void)buf;
}

int remove(const char *path)
{
    int s = find_store(path);
    if (s < 0 || store[s].fixed)
        return -1;
    free(store[s].data);
    store[s].used = 0;
    return 0;
}

int unlink(const char *path)
{
    return remove(path);
}

int rename(const char *from, const char *to)
{
    int s = find_store(from);
    if (s < 0 || store[s].fixed)
        return -1;
    remove(to);
    strncpy(store[s].name, base_name(to), NAME - 1);
    return 0;
}

int access(const char *path, int mode)
{
    (void)mode;
    return find_store(path) < 0 ? -1 : 0;
}

/* For fscanf (printf.c): the rest of a file as a string, and taking
 * what was used. */
const char *file_rest(FILE *f, long *n)
{
    if (!f || f->kind != 1) {
        *n = 0;
        return "";
    }
    *n = store[f->store].size - f->at;
    return (const char *)store[f->store].data + f->at;
}

void file_advance(FILE *f, long n)
{
    if (f && f->kind == 1)
        f->at += n;
}
