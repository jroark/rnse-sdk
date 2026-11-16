/* What the C library of sdk/libc wants of the program it is linked
 * into, and what it offers beyond the standard.  A bare-metal program
 * that uses it (doom/, umac/, quake/) defines port_idle and port_exit. */
#ifndef RNSE_LIBC_H
#define RNSE_LIBC_H

/* Every wait (usleep) calls it: the faceplate, the display key, CD
 * EJECT, RETURN held, whatever else the program keeps going. */
void port_idle(void);
/* exit(), abort() and failed assertions end here: status 0 for a
 * normal end; anything else after the error (error_text, what went to
 * stderr) has been shown. */
void port_exit(int status) __attribute__((noreturn));

/* What went to stderr, kept for port_exit to show. */
extern char error_text[1024];
/* A file in RAM, put in place before anything opens it (fopen finds it
 * by its base name; it is read-only to the program). */
void files_add_fixed(const char *name, void *data, long size);
/* A read-only file whose bytes come from the program, read_at(at, buf,
 * n) giving up to n of them from `at` on (Quake's PAK, from the card). */
void files_add_reader(const char *name, long size, long (*read_at)(long at, void *buf, long n));

#endif
