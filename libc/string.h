#ifndef RNSE_DOOM_STRING_H
#define RNSE_DOOM_STRING_H
#include <stddef.h>
void *memcpy(void *to, const void *from, unsigned long n); /* as rnse.h */
void *memmove(void *to, const void *from, size_t n);
void *memset(void *p, int c, unsigned long n); /* as rnse.h */
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *p, int c, size_t n);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t n);
char *strcpy(char *to, const char *from);
char *strncpy(char *to, const char *from, size_t n);
char *strcat(char *to, const char *from);
char *strncat(char *to, const char *from, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *s, const char *find);
char *strdup(const char *s);
char *strerror(int e);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
#endif
