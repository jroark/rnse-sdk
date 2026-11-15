/* Resident app ABI 1. Native code is trusted and callbacks must return. */
#ifndef RNSE_APP_H
#define RNSE_APP_H
#include "rnse.h"
#define APP_BASE 0xac410000u /* uncached alias of reserved RAM 0x8c410000 */
#define APP_CAPACITY 0x80000u
#define APP_MAGIC 0x524e4131u
#define APP_ABI 1u
enum { APP_TURN = 1, APP_SELECT, APP_TL, APP_TR, APP_BL, APP_BR };
/* All words big endian; checksum is FNV-1a of the image, treating word 6 as
 * zero. Callback offsets are relative to APP_BASE, including this 64-byte
 * header. */
typedef struct {
    u32 magic, abi, model, bytes, bss_end, base, checksum;
    u32 init, input, update, draw, shutdown;
    char title[16];
} RnseAppHeader;
typedef struct {
    u32 abi;
    /* Coordinates relative to content area, 400 x 136. Clipped by host. */
    void (*fill)(int x, int y, int w, int h, u8 colour);
    void (*text)(int x, int y, const char *text, u8 colour, int scale);
    u32 (*time_ms)(void);
    /* palette: 0 background, 1 white, 2 muted, 3 red, 4 panel */
} RnseAppHost;
#endif
