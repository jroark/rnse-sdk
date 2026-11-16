/* setjmp and longjmp, as GCC's builtins: enough for a jump out of deep
 * code back to a caller that is still running (umac's fatal errors).
 * setjmp says 1 after any longjmp, whatever value it was given, so
 * test it only for nonzero; and longjmp must not be called from the
 * function that called setjmp itself. */
#ifndef RNSE_SETJMP_H
#define RNSE_SETJMP_H

typedef void *jmp_buf[5];
#define setjmp(b) __builtin_setjmp(b)
#define longjmp(b, v) ((void)(v), __builtin_longjmp((b), 1))

#endif
