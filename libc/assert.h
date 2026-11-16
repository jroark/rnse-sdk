#ifndef RNSE_DOOM_ASSERT_H
#define RNSE_DOOM_ASSERT_H
void rnse_assert(const char *what, const char *file, int line) __attribute__((noreturn));
#define assert(x) ((x) ? (void)0 : rnse_assert(#x, __FILE__, __LINE__))
#endif
