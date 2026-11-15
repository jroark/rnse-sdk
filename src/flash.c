/* The flash memory of the 192, for one purpose: to take a program that
 * was installed in the operating system's place (`rnse firmware`) out
 * again.  The boot ROM starts whatever is there with a right checksum
 * under a directory whose first tag is ".OS "; with the directory gone
 * it takes the unit for empty and installs from the update disc in the
 * drive (docs/boot-rom.md).  The directory is the 8 KiB sector at
 * 0xc000, right above the boot ROM's six.
 *
 * So this erases one sector and no other, whatever it is asked: the
 * sectors below it are the boot ROM, which nothing brings back.
 *
 * The commands are the stock loader's, word for word (two Fujitsu
 * chips with the AMD command set on a 32-bit bus). */
#include "rnse.h"

#define REG32(a) (*(volatile u32 *)(a))
#define FLASH 0xa0000000
#define DIRECTORY 0x0000c000

#define REG8(a) (*(volatile u8 *)(a))
#define STROBE 0xb2300001 /* port 0, bit 0: an edge keeps the watchdog quiet */

int firmware_remove(void)
{
    u32 i, rounds;
    if (screen_width != 400)
        return -1;
    REG32(FLASH + 0x1554) = 0xaa;
    REG32(FLASH + 0x0aa8) = 0x55;
    REG32(FLASH + 0x1554) = 0x80;
    REG32(FLASH + 0x1554) = 0xaa;
    REG32(FLASH + 0x0aa8) = 0x55;
    REG32(FLASH + DIRECTORY) = 0x30;
    /* A sector takes a quarter of a second by the chip's own account,
     * and while it does the chip answers with its status instead of
     * its data: done is when the sector reads as erased, twice.  No
     * clock is used and the watchdog is served here, so that this
     * works before anything is set up (firmware_guard).  A round is
     * three accesses to the bus, a third of a microsecond at the
     * least: forty million of them are ten seconds and more. */
    for (rounds = 0;; rounds++) {
        REG8(STROBE) ^= 1;
        if (REG32(FLASH + DIRECTORY) == 0xffffffff && REG32(FLASH + DIRECTORY) == 0xffffffff)
            break;
        if (rounds > 40000000) {
            REG32(FLASH) = 0xf0; /* back to reading, whatever state it is in */
            return -2;
        }
    }
    for (i = 0; i < 0x2000; i += 4)
        if (REG32(FLASH + DIRECTORY + i) != 0xffffffff)
            return -3;
    return 0;
}

/* The guard of a program in flash.  Such a program is started by the
 * boot ROM at every start and nothing in the ROM takes it away if it
 * does not work (tried: a program that hangs is reset by the watchdog
 * and started again, for ever).  So the program counts its own starts,
 * in the battery-backed memory, above everything the stock software
 * uses there (which ends at 0x3f630): every start counts one up, and
 * firmware_healthy(), which the program calls when it has run for a
 * while, sets the count back.  At the fifth start in a row that did
 * not get that far the guard takes the program out and restarts, and
 * the boot ROM installs from the update disc in the drive.
 *
 * That is the way out of a program that hangs or dies, and of one that
 * runs but cannot be operated: switch the unit off and on four times,
 * each time before the program calls itself healthy.
 *
 * crt0 calls this before main, with the first of the start's
 * arguments: `rnse firmware` has the start-up instructions pass
 * FIRMWARE_MARK there, and only then is anything done. */
#define FIRMWARE_MARK 0x464c5348 /* "FLSH" */
#define GUARD_MARK 0x47524431    /* "GRD1" */
#define GUARD ((volatile u32 *)0xb003ff00)
#define GUARD_STARTS 5

static int installed;

void firmware_guard(u32 arg)
{
    u32 count;
    if (arg != FIRMWARE_MARK)
        return;
    installed = 1;
    count = GUARD[0] == GUARD_MARK && GUARD[1] == ~GUARD[2] ? GUARD[1] : 0;
    count++;
    GUARD[0] = GUARD_MARK;
    GUARD[1] = count;
    GUARD[2] = ~count;
    if (count < GUARD_STARTS)
        return;
    GUARD[1] = 0;
    GUARD[2] = ~0u;
    firmware_remove();
    restart();
}

int firmware_installed(void)
{
    return installed;
}

void firmware_healthy(void)
{
    if (!installed)
        return;
    GUARD[0] = GUARD_MARK;
    GUARD[1] = 0;
    GUARD[2] = ~0u;
}
