#ifndef RNSE_DOOM_STDLIB_H
#define RNSE_DOOM_STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void *malloc(unsigned long n); /* as the SDK's rnse.h has it */
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);
void free(void *p);
int atoi(const char *s);
long atol(const char *s);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
int abs(int v);
long labs(long v);
void exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
int atexit(void (*f)(void));
char *getenv(const char *name);
int system(const char *command);
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
#endif
