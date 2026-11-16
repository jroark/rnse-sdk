/* The C library of Doom on the RNS-E (doom/port/libc): what the game
 * uses and no more.  Output goes to the SDK's debug serial port (the
 * emulator prints it); stderr is kept as well, for the screen that an
 * error ends on.  Files are the WAD in RAM (read only) and a few more
 * that live in RAM for the session: the settings, saved games. */
#ifndef RNSE_DOOM_STDIO_H
#define RNSE_DOOM_STDIO_H
#include <stddef.h>
#include <stdarg.h>

typedef struct rnse_file FILE;
extern FILE *stdin, *stdout, *stderr;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define BUFSIZ 512

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
size_t fread(void *buf, size_t size, size_t n, FILE *f);
size_t fwrite(const void *buf, size_t size, size_t n, FILE *f);
int fseek(FILE *f, long offset, int whence);
long ftell(FILE *f);
int fflush(FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
int fgetc(FILE *f);
#define getc fgetc
char *fgets(char *s, int n, FILE *f);
int fputc(int c, FILE *f);
#define putc fputc
int fputs(const char *s, FILE *f);
int puts(const char *s);
int putchar(int c);
int fileno(FILE *f);
void setbuf(FILE *f, char *buf);
int remove(const char *path);
int rename(const char *from, const char *to);

int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int sprintf(char *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *s, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int vsprintf(char *s, const char *fmt, va_list ap);
int vsnprintf(char *s, size_t n, const char *fmt, va_list ap);
int sscanf(const char *s, const char *fmt, ...);
int fscanf(FILE *f, const char *fmt, ...);
int vsscanf(const char *s, const char *fmt, va_list ap);

#endif
