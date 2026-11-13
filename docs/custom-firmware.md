# Writing your own software for the unit

`sdk/` holds what is needed to write a program from scratch, run it in
the emulator and put it on a disc for a real unit: start-up code, a
small hardware layer for each generation of the unit, and an example.

Read the section "What this does and does not establish" before
taking a disc to a car.  Nothing here has been run on real hardware.

## Where the program runs

In the place of the update loader.  An update disc carries, for each
kind of unit, a loader (`LDM`) that the unit's boot ROM copies from the
disc into RAM and starts; the loader then does the update.  A program
of your own in that place

* is started by the boot ROM itself, with none of the unit's software
  underneath it;
* runs from RAM and writes nothing to the flash unless it chooses to,
  so the unit's own software is still there when the disc comes out.

Replacing the installed software itself (the `OSM` module and the
others) is also possible, with `rnse repack`, but then the program must
survive the update procedure and a mistake leaves a unit that does not
start.  Start here.

## Building and running

You need an SH-4 GCC that can produce big-endian code without its
libraries; Debian's and Ubuntu's `gcc-sh4-linux-gnu` will do.

    sdk/build.sh 193                 # or 192; writes sdk/out/hello-193.bin
    rnse custom disc.iso V574 sdk/out/hello-193.bin hello.iso
    rnse loader hello.iso --variant V574 --strict --screenshot hello.png

`rnse custom` takes an image of an update disc you own and writes a
copy in which the loader of one variant is your program.  The loader is
on the disc twice (inside `LOADING.KWI`, and as a file of its own that
the boot ROM reads); both are replaced.  The module's header is the
stock loader's with a new checksum, and the image keeps the stock
loader's length, so the file differs from one the boot ROM is known to
accept in its contents only.

`rnse loader` starts the program as the emulated boot ROM starts the
loader.  What the program sends to the debug serial port is printed;
`--screenshot`, `--shot`, `--io-log` and the other options work as for
the stock software, and `rnse ui hello.iso --variant V574 --loader`
shows it in the browser front end.

Variants: `V220` and `V222` are the 192 (Europe, North America), `V574`,
`V575` and `VB48` the 193.  Use the one your unit has; its loader's
header names the project the boot ROM expects.

## What a program gets

`sdk/include/rnse.h`.  In short: `board_init()` brings the board up and
starts a 10 ms tick; then there is a clock (`millis`, `delay_ms`), text
on the debug serial port, and a picture of 8 bits per pixel through a
palette (`screen_pixel`, `screen_fill`, `screen_text`).  The example,
`sdk/examples/hello`, is forty lines.

| | 192 | 193 |
|---|---|---|
| CPU | SH7750R | SH-4A core of an SH7770-family chip |
| linked at | 0x8df00080 | 0x09b60080 |
| room (code, data, stack) | 0x88000 bytes | 0x2d8000 bytes |
| picture | 400 x 235, through the Coral chip | 800 x 480, display unit plane 6 |
| debug serial port | companion chip, channel 5 | on-chip SCIF at 0xffe40000 |
| tick | TMU0, INTEVT 0x400, level 7 | the same, through INT2 |
| watchdog strobe | port 3 bit 0, toggled by the tick | 0xffc44008 bit 0, toggled by the tick |

The program keeps to the memory the stock loader's image occupies.  It
uses no service of the boot ROM.

There is no C library.  The compiler's own support routines are not
there either, so `/` and `%` on variables will not link: use `udiv` and
`umod`.  Floating point is switched off (`-m4-nofpu`).

## How the hardware layers were written

For neither unit is there a board schematic, and for the 193's chip no
public manual.  `sdk/src/hw192.c` and `hw193.c` therefore do what the
stock loader does: every register they write is one the stock loader
writes, with the same value, in the same order, as recorded from the
loader running in the emulator.  Writes whose purpose is unknown are
repeated all the same and marked as such in the comments, because a
real unit may need them even though the emulator does not.

Two things found on the way that a program has to know:

* On the 192 the graphics chip is little-endian on a big-endian bus:
  pixel N of an 8-bit surface is the byte at address N XOR 3.
* The stock loader never sets the direction of the ports it drives, so
  the boot ROM has done so before it starts.  A program in the loader's
  place inherits that, like the rest of the boot ROM's set-up (memory
  controller, clocks, the drive).

## What the emulator checks, and `--strict`

The emulator runs the program on its models of the CPU, the timers, the
interrupt controllers, the display hardware and so on.  Those models
were built from the behaviour of the stock software; they are right
where that software exercises them and unknown elsewhere.

`--strict` lists, when the run ends, every address the program used
that no model answers for:

    strict: the software used hardware the emulator has no model of (30 addresses).
            What is written there is stored and read back; real hardware may differ.
            fe00802c: 0 reads, 1 writes, first at pc 09b60548
            ...

For the example that list is the set-up writes copied from the loader
(interrupt pins, ports, the serial port's format and clock registers).
Anything else that shows up there in a program of yours is something
the emulator cannot tell you about.

It also enforces a few things real hardware does: an address error for
a misaligned access, the 193's external watchdog (a program that stops
toggling the strobe for 1.5 s is reset; the real limit is not known),
interrupts only when unmasked in the controller.

## What this does and does not establish

Established, by the tests in `tests/disc.rs`: the example builds, is
accepted by the same checks the stock modules pass (header, checksum),
starts in the emulator the way the stock loader does, and behaves as
written on both units.

Not established:

* **That a real boot ROM starts it.**  The boot ROM is not on the update
  disc and has not been read out; the emulator's is a stand-in.  How it
  chooses the loader file, what it checks beyond the header (it might
  compare versions with what is installed, as the loader does), and
  what state it leaves are inferred from the stock loaders.
* **That the bring-up is sufficient.**  It is the stock loader's, minus
  the parts the example has no use for (the drive, CAN, DMA).  If the
  picture needs one of those, or needs the loader's exact timing, the
  emulator will not show it.
* **Timing.**  The emulated CPU executes one instruction per clock
  cycle at an estimated clock; the watchdog's real period, the display's
  real requirements and the serial port's real bit rate are unknown.
* **Everything the stock loader does not use.**  Sound, the faceplate
  in loader mode, the radio processor, GPS: no stock loader touches
  them, so there is nothing to copy the set-up from yet.

If you do try it on a unit: the program writes nothing to flash, so the
worst expected outcome is that nothing happens or the unit restarts
when the watchdog expires.  The debug serial port is a header inside
the unit; its location and bit rate are not known.  What would help
most is a report of what the screen shows, and, from an opened unit,
the markings of the chips.
