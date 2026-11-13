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
palette (`screen_pixel`, `screen_fill`, `screen_text`), and the keys and
knobs of the faceplate (`panel_init`, `panel_poll`, `panel_key`,
`panel_knob`).  Two examples: `sdk/examples/hello` draws a test picture
and counts seconds; `sdk/examples/keys` shows what the faceplate
reports (`sdk/build.sh 193 keys`, and `--key SETUP@1500 --knob 1:3@2500`
on the `rnse loader` command line to press and turn).

| | 192 | 193 |
|---|---|---|
| CPU | SH7750R | SH-4A core of an SH7770-family chip |
| linked at | 0x8df00080 | 0x09b60080 |
| room (code, data, stack) | 0x88000 bytes | 0x2d8000 bytes |
| picture | 400 x 235, through the Coral chip | 800 x 480, display unit plane 6 |
| debug serial port | companion chip, channel 5 | on-chip SCIF at 0xffe40000 |
| tick | TMU0, INTEVT 0x400, level 7 | the same, through INT2 |
| faceplate | serial channel 3, reset on latch 0x16000004 bit 0 | SCIF at 0xff926000, reset on port 0xff909004 bit 1 |
| watchdog strobe | port 0 bit 0 (0x12300001), toggled by the tick | 0xffc44008 bit 0, toggled by the tick |

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

The faceplate is the exception, because no loader uses it: its port
set-up, the release of its controller's reset line and the settings
sent to the controller are the stock operating system's instead.  Among
those settings are six 12-bit levels (command 0x06) that the operating
system ramps while it fades the lighting in; the SDK sends the final
values, since a unit that never gets them may well stay dark.

Things found on the way that a program has to know:

* On the 192 the graphics chip is little-endian on a big-endian bus:
  pixel N of an 8-bit surface is the byte at address N XOR 3.
* The faceplate controller is held in reset until the program releases
  it, and says nothing until then.
* The stock loader never sets the direction of the ports it drives, so
  the boot ROM has done so before it starts.  A program in the loader's
  place inherits that, like the rest of the boot ROM's set-up (memory
  controller, clocks, the drive).

## What the emulator checks, and `--strict`

The emulator runs the program on its models of the CPU, the timers, the
interrupt controllers, the display hardware and so on.  Those models
were built from the behaviour of the stock software; they are right
where that software exercises them and unknown elsewhere.

`--strict` does three things.  It starts the machine with arbitrary
data in RAM instead of zeroes, as after power-up (the stock software
boots either way; a program that forgets to clear what it uses will
not).  It lets the watchdog bite from the first moment, not only once
the program has begun to strobe it, and says so when the strobe was
ever left alone for longer than the stock software leaves it.  And it
lists, when the run ends, every address the program used that no model
answers for:

    strict: the software used hardware the emulator has no model of (16 addresses).
            What is written there is stored and read back; real hardware may differ.
            fe00802c: 0 reads, 1 writes, first at pc 09b60564
            ...

For the example on the 193 that list is the set-up writes copied from
the loader whose meaning is unknown (pin functions, clock control); on
the 192 it is a single latch.  Anything else that shows up there in a
program of yours is something the emulator cannot tell you about.  The
stock software's own list is a measure of what is left to do: 15
addresses on the 192, some 110 on the 193 in the first 20 seconds.

The emulator also enforces a few things real hardware does whether or
not `--strict` is given:

* an address error for a misaligned access, the illegal-instruction
  exceptions (also for branches in delay slots);
* the external watchdog of both units: once a program has started
  toggling the strobe, 1.5 s without an edge resets the unit.  The real
  limit is not known.  What is known is what the stock software does:
  an edge every 20 ms from its tick, and never more than about 220 ms
  between two (192) or 150 ms (193), from the first instruction on.  A
  program that keeps to that is as safe as the stock software;
* interrupts only when unmasked in the controllers, with the stock
  priorities;
* serial ports send only with the transmitter on and hear only with the
  receiver on and the FIFO out of reset; the faceplate controller is
  silent until its reset line is released.

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
* **The faceplate in the loader's place.**  The link's set-up is the
  operating system's, but the operating system runs after the boot ROM
  has started every module; whether the pins of that serial port and
  the reset line are already switched to those functions when a loader
  runs is not known.  The emulated controller is a stand-in that knows
  the frames the stock software exchanges with it; how long the real
  one takes to start, and whether it insists on every setting, is not
  known either.
* **Everything else the stock loader does not use.**  Sound, the radio
  processor, GPS: no stock loader touches them, and the SDK has no code
  for them yet.

If you do try it on a unit: the program writes nothing to flash, so the
worst expected outcome is that nothing happens or the unit restarts
when the watchdog expires.  The debug serial port is a header inside
the unit; its location and bit rate are not known.  What would help
most is a report of what the screen shows, and, from an opened unit,
the markings of the chips.
