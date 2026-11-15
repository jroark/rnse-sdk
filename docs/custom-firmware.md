# Writing your own software for the unit

`sdk/` holds what is needed to write a program from scratch, run it in
the emulator and put it on a disc for a real unit: start-up code, a
small hardware layer for each generation of the unit, and an example.

Read the sections "On a real unit" and "What this does and does not
establish" before taking a disc to a car.  The 192's side has run on
one real unit, a North American one on a bench; the 193's has not run
on any.

## Where the program runs, and what starts it

In the place of the update loader.  An update disc carries, for each
kind of unit, a loader (`LDM`).  When the unit's installed software
finds such a disc in the drive, and the disc's software index is higher
than its own, it checks the disc, copies the loader into RAM and calls
it; the loader then does the update.  (`docs/boot-rom.md`, "Starting
the loader", has the steps.)  A program of your own in that place

* is started by the unit's own software, after the same checks a stock
  loader passes, and from then on has the machine to itself;
* runs from RAM and writes nothing to the flash unless it chooses to,
  so the unit's own software is still there when the disc comes out.

All of that can be watched in the emulator, with the stock software
doing the starting:

    rnse custom disc.iso V574 sdk/out/hello-193.bin hello.iso --index 0230
    rnse boot hello.iso --variant V574 --ignition --installed-index 0220 \
        --cycles 8000000000 --screenshot hello.png

The index is what makes the disc an update.  The installed software
compares the second and third digits of the disc's four with its own
and wants the disc's higher: a unit with 0650 takes 0660, not 0651.
`rnse info` shows a disc's index for each variant; a unit's own is the
`SOFIDX` of the update it last took, and that is the number its version
display shows as `SW:` (a unit that took a disc with software 0371 and
index 0350 showed 0350, and 0360 after the same software with that
index).  The same disc with an index that is not higher is
ignored and the unit starts as always.

A program started this way finds the machine as the running operating
system left it, not as after a reset: interrupts are open, timers run,
the picture is the system's.  The SDK's start-up code masks interrupts
with its first instructions and sets everything it uses itself, as the
stock loaders do.

Replacing the installed software itself (the `OSM` module and the
others) is also possible, with `rnse repack`, but then the program must
survive the update procedure and a mistake leaves a unit that does not
start.  Start here.

## The way back

The installed software starts the program at *every* start for as long
as the disc is in the drive, and it does so before it would react to
the eject key (tried in the emulator: the key is ignored in those first
seconds).  The disc sits behind the motorised display, which the
program does not move.  So a program that just runs is a trap: the unit
is not damaged, but it is the program's until the disc is out, and the
disc does not come out.

What ends that is the stock loader, because an update leaves the unit
with the disc's software index, and then the disc is no update any
more.  `rnse custom` therefore packs the stock loader it replaces into
the image (twice, in separately checked blocks), and `stock_update()`
in the SDK unpacks it, puts it where the program is and calls it with
the arguments the program was started with.  The unit then does an
ordinary update from the disc, restarts with its ordinary software and
gives the disc back when asked.

Things to know about it:

* It installs the software *on the disc*.  Make the custom disc from
  the update disc of the software the unit has or should have.  A disc
  older than the unit's software would take the unit back to it.
* Each round uses up an index: afterwards the unit has the custom
  disc's index, and the next custom disc needs a higher one still.
* It needs nothing but the processor and RAM until the stock loader
  runs: no key, no command of the program's own to the drive.
  `sdk/examples/probe` does nothing but show a picture, count down a
  minute and call it, and is the program to try first.
* A program that hangs before it gets there is still a trap.  Keep the
  call on a path that cannot be blocked: a timer, as in the probe.

On a bench there is a cheaper end: `restart()` goes to the boot ROM's
reset code and changes nothing.  The unit starts its software, the
software finds the disc again and starts the program again, round and
round, and the unit's index stays what it was, so one disc serves for
any number of tries; the disc comes out on the eject key in the half
minute before each start.  The probe and the drive's log can be built
to end that way (`-DRESTART`), and the drive's log restarts on RETURN.
In the emulator this needs a real boot ROM in the stand-in's place
(`--boot-rom`; the stand-in stops the run at a reset), and then it goes
round as on the unit, `--strict` included.  (It did not at first with
the drive given its times: the end of one DMA transfer was marked on
the next, which the software had already set up, and that one never
started.  `src/sh4/onchip.rs`, `dma_finish`.)

The emulator runs the whole circle, with the stock software at both
ends (`rnse boot probe.iso --installed-index ...`, and the test
`custom_program_hands_back_to_the_stock_loader`), and with the
processor's caches modelled (`--strict`, and always in that test).  The
hand-over replaces running code, which is where caches bite: the stub
purges the data cache over the place, invalidates the instruction cache
and copies through uncached addresses.  Take the invalidation out and
the 192 fails in the emulator as it would on the bench, executing what
is left of the program from the instruction cache.

## Programs from an SD card

A disc for every program to try is slow and costs a disc.  On the 192
there is another way once one disc is made: `sdk/examples/sdboot` is a
loader that goes on that disc, and what it starts is the file
`RNSE.BIN` in the root directory of a FAT card in either slot, a
program as `build.sh` makes it.  The stock software starts the loader
from the disc as it starts any program there; the loader reads the
file, puts it in its own place and starts it with the arguments it was
started with, and with the packed stock loader still where `rnse
custom` put it, so the program's `stock_update` works.  A program that
ends in `restart()` is back at the loader half a minute later, with
whatever is on the card by then.

    sdk/build.sh 192 sdboot
    rnse custom disc.iso V222 sdk/out/sdboot-192.bin sdboot.iso --index 0380
    sdk/build.sh 192 hello && mkdir card && cp sdk/out/hello-192.bin card/RNSE.BIN
    rnse card card.img card
    rnse boot sdboot.iso --variant V222 --ignition --installed-index 0370 \
        --sd-card card.img --cycles 9000000000 --screenshot hello.png

The cards are the card processors' (`docs/hardware.md`, "SD card
processors"), and the SDK has no program for those: `sd_init`
(`sdk/src/sd.c`) loads the installed software's own, out of the unit's
flash, the way that software's driver loads it, and then says to it
what that driver says.  So it works with one installed software, the
North American 0371, and looks before it starts whether that is what
the flash holds.  `tools/dsp-recipe.py` makes the table by which the
download is put together from the flash, out of a trace of the stock
driver (`rnse boot --sd-card ... --trace-dsp`) and a flash image, and
checks that the table rebuilds every word; another software needs its
own table.  `run_image` starts an image in the running program's place.

`sd_read` reads a sector, `sd_load` a file from a FAT12, FAT16 or FAT32
card.

In the emulator all of it runs, both slots, with the stock software and
a unit's boot ROM at both ends (`tests/disc.rs`,
`programs_come_from_an_sd_card`).  On a unit (a North American 192
with software 0371, a 32 MB card formatted FAT16 by macOS, first slot)
it has run as far as this: the card processor took its program, the
card started in 3.99 s, the file was found and read in 40 ms and the
program ran from it and restarted the unit when it was done.  Three
things a unit taught on the way:

- **A card's start wants the stock driver's pace.**  That driver leaves
  150 ms between any two steps.  With the steps one behind the other
  the card processor reported the first application command as
  unanswered, in both slots (the same answer an empty slot gives), and
  with the pace the same card started.  Which step it was that needed
  the time was not looked for.
- **A card that does not answer CMD8** (version 1 of the specification,
  as small old cards are) is asked ACMD41 without the claim to know
  high-capacity cards, again and again until it is ready.  The
  emulator's card can be one (`--sd-old`).
- **A loader must leave the faceplate controller alone, or go on
  talking to it.**  The screen's light is the controller's.  A loader
  that started the controller anew (`panel_init`) and then read a card
  for four seconds without `panel_poll` handed a dark screen to a
  program that draws and never talks to the controller: the unit
  showed nothing for the program's whole run.  The emulator shows the
  same, and the test looks at the screen now.  `sdboot` starts the
  controller only when it has found nothing and wants the keys.

What has not run on a unit: the second slot with a card that starts, a
high-capacity card, FAT12 and FAT32 (in the emulator each reads a card
image formatted by macOS), and the round from a program's restart back
into the loader more than once.

## Building and running

You need an SH-4 GCC that can produce big-endian code without its
libraries; Debian's and Ubuntu's `gcc-sh4-linux-gnu` will do.  Where
there is none to install (macOS), `tools/sh4-docker` runs that one in a
container: `PATH=$PWD/tools/sh4-docker:$PATH sdk/build.sh 192`.

    sdk/build.sh 193                 # or 192; writes sdk/out/hello-193.bin
    rnse custom disc.iso V574 sdk/out/hello-193.bin hello.iso
    rnse loader hello.iso --variant V574 --strict --screenshot hello.png

`rnse custom` takes an image of an update disc you own and writes a
copy in which the loader of one variant is your program.  The loader is
on the disc twice (inside `LOADING.KWI`, where the installed software
takes it from, and as a file of its own); both are replaced.  The module's header is the
stock loader's with a new checksum, and the image keeps the stock
loader's length, so the file differs from one the boot ROM is known to
accept in its contents only.

`rnse loader` starts the program directly, without the installed
software: quicker, and the way to use `--strict` and `--gdb` from the
first instruction.  What the program sends to the debug serial port is printed;
`--screenshot`, `--shot`, `--io-log` and the other options work as for
the stock software, and `rnse ui hello.iso --variant V574 --loader`
shows it in the browser front end.

Variants: `V220` and `V222` are the 192 (Europe, North America), `V574`,
`V575` and `VB48` the 193.  Use the one your unit has; its loader's
header names the project the boot ROM expects.

## Debugging

`--gdb PORT` makes `rnse loader` (or `boot`) wait for GDB instead of
running:

    rnse loader keys.iso --variant V574 --gdb 3333 &
    gdb-multiarch sdk/out/keys-193.elf -ex 'target remote :3333'
    (gdb) break panel_poll
    (gdb) continue
    (gdb) bt
    (gdb) monitor screenshot now.png

The SDK builds with debug information, so breakpoints, stepping,
backtraces and variables work at source level; any GDB that knows the
SH-4 will do (`gdb-multiarch` on Debian and Ubuntu).  The machine runs
only while GDB lets it; Ctrl-C stops it.  Memory GDB can read is RAM and
flash; device registers are not shown, because reading them has
effects.  `monitor time` prints the emulated time, `monitor console`
what the program has sent to its debug port, `monitor screenshot FILE`
writes the picture.  After `detach` the emulator waits for the next
connection with the machine as it was; `kill` ends the run.  There are
no watchpoints yet (GDB then steps instead, slowly).

## What a program gets

`sdk/include/rnse.h`.  In short: `board_init()` brings the board up and
starts a 10 ms tick; then there is a clock (`millis`, `delay_ms`), text
on the debug serial port, and a picture of 8 bits per pixel through a
palette (`screen_pixel`, `screen_fill`, `screen_text`), the way back to
the stock software (`stock_update`, above), and the keys and
knobs of the faceplate (`panel_init`, `panel_poll`, `panel_key`,
`panel_knob`), and the CD drive (`cd_init`, `cd_wait_ready`, `cd_read`,
`cd_find` for a file in the root directory of the disc), and the car's
CAN bus (`can_init`, `can_send`, `can_receive`).  Seven examples
(`sdk/examples/report` and `sdk/examples/drive` have sections of their
own below):
`sdk/examples/probe` is the one for a first try on a unit (see "The way
back"); `sdk/examples/hello` draws a test picture and counts seconds;
`sdk/examples/keys` shows what the faceplate reports (`sdk/build.sh 193
keys`, and `--key SETUP@1500 --knob 1:3@2500` on the `rnse loader`
command line to press and turn); `sdk/examples/cd` reads the disc the
program came on; `sdk/examples/beacon` is the
probe that says on the CAN bus how far it has got, for a logic
analyser on the two wires and no other node ("On a real unit");
`sdk/examples/can` lists the frames on the bus and
sends the one with which the stock software reports itself (`--can
351:4500000000000000@300` on the command line puts a frame on the
emulated bus every 300 ms, `--can-alone` takes every other node away).

| | 192 | 193 |
|---|---|---|
| CPU | SH7750R | SH-4A core of an SH7770-family chip |
| linked at | 0x8df00080 | 0x09b60080 |
| room (code, data, stack) | 0x20000 bytes | 0x100000 bytes |
| above that, to the end of the stock loader's image | the stock loader, packed | the same |
| picture | 400 x 235, through the Coral chip | 800 x 480, display unit plane 6 |
| debug serial port | companion chip, channel 5 | on-chip SCIF at 0xffe40000 |
| tick | TMU0, INTEVT 0x400, level 7 | the same, through INT2 |
| faceplate | serial channel 3, reset on latch 0x16000004 bit 0 | SCIF at 0xff926000, reset on port 0xff909004 bit 1 |
| CD drive | ATA registers at 0x07800001 + 2n, data at 0x07880000 | chip's ATAPI interface at 0xffc62100 |
| CAN | HCAN2 at 0x07c00000 | on-chip controller at 0xffdf0000, other mailbox layout |
| watchdog strobe | port 0 bit 0 (0x12300001), toggled by the tick | 0xffc44008 bit 0, toggled by the tick |

The program keeps to the memory the stock loader's image occupies.  It
uses no service of the boot ROM.

Not in the kit: sound.  On the 192 it is behind the two signal
processors of the card board, like the SD cards, which the kit does
read: see "Programs from an SD card".

There is no C library.  The compiler's own support routines are not
there either, so `/` and `%` on variables will not link: use `udiv` and
`umod`.  Floating point is switched off (`-m4-nofpu`).

## A report from a unit, and its boot ROM

`sdk/examples/report` is the probe with a purpose: before it sets
anything up it reads the processor's own registers as the installed
software and the boot ROM have left them (identification, clock
ratios, bus set-up, timers, interrupt priorities, serial port
settings; on the 193 only registers its stock software uses itself),
and it reads the boot ROM, the one part of the unit's software that is
on no disc.  On a 192 it then asks the two processors of the card
board about themselves (below).  Then it shows all of it and sends all
of it:

* on the screen, page after page, twelve lines of address and value to
  a page, on a 192 a page for each card processor, then the state it
  was started in (SR, VBR, where its caller was, the stack pointer, the
  arguments) and check sums of the boot ROM, the whole and each 4 KiB of it.  Photographs of the pages are a
  complete report of the registers.  Built with
  `EXTRA_CFLAGS=-DHEXPAGES` it also shows the ROM itself, 784 bytes a
  page with a check byte at the end of every line;
* on the CAN bus, if anything there acknowledges frames (an adapter at
  100 kbit/s that is not in listen-only mode; the transceiver in the
  unit is a fault-tolerant low-speed one): identifiers 0x7F0 to 0x7F4,
  round after round, a round of the 192's 48 KiB taking some 15
  seconds.  `rnse report log.txt --rom rom.bin` puts a log of the
  frames back together (candump's formats and a few others), fills
  what one round lost from the next, checks the ROM against the check
  sum the unit computed, names the registers and says what the clock
  register means;
* on the debug serial port, the registers and the ROM's check sum.

After six rounds of the pages (`-DROUNDS=n`, `-DPAGE_SECONDS=n`) it
hands over to the stock loader like the probe, so the same disc and
the same procedure apply: build it, put it on a copy of the unit's
update disc with a higher index, and the unit updates itself from that
disc afterwards.

    sdk/build.sh 192 report
    rnse custom disc.iso V220 sdk/out/report-192.bin report.iso --index 0660
    candump -L can0 > log.txt          # on the bench, while it runs
    rnse report log.txt --rom bootrom-192.bin

### The card processors of the 192

The two signal processors behind the SD slots (NEC µPD77115A,
`docs/hardware.md`, "SD card processors") do nothing until a program
is loaded into them.  The report loads one of its own into each, some
thirty instructions written out in `sdk/examples/report/dsp.c` (the
stock software's program is not used and not needed), which

* sends the levels on the processor's eight port pins.  After a reset
  they are all inputs, and the board's straps show: pins 7 to 4 are the
  multiplier of the processor's clock, pins 1 and 0 its boot mode;
* sends its clock control and host status registers;
* runs a loop of 1024 x 32768 clocks on its clock input, which is the
  22.5792 MHz crystal of the card board, and says when it is done;
* switches to the multiplied clock, as the stock program does, and
  runs the loop again.

The main processor times both loops with its timer 0.  A crystal with
its frequency printed on it against that timer gives **the main
processor's peripheral clock**, independently of the clock register:
1.486 s is 21.85 million counts at 58.8 MHz and half that at 29.4.  The
two loops against each other give the multiplier a second time, and
whether an instruction of that loop takes one clock, as the manual
says and the emulator has it.  `rnse report` says all of this in
words.  Nothing is written to a card, the processors' pins stay
inputs, and both are back in reset when the pages start.

What the emulator answers, which is what it assumes and the report
would confirm or correct: pins `3f` (multiplier 3 for 67.7 MHz, host
word boot, pins 3 and 2 high), clock control 0, and the two times in
the ratio 3.  A result other than 1 on a processor's page says how far
the questions got (`dsp.h`): `e1` means its boot ROM took no word at
all, which would say the reset line or the port are not where the
emulator has them.

### The drive's log

`sdk/examples/drive` is a third program for a real unit, about one
question: what the drive answers while a disc goes in.  The stock
software takes a disc that is put in while it runs for an update only
if the drive answers in a particular way while it loads the disc.  The
emulator's drive answers the one way that suits all the software seen
(`docs/boot-rom.md`, "A disc that goes in while the unit runs"); that
is an inference, and this program is the measurement.

The program runs from memory, so its disc can leave.  It asks the
drive ten times a second whether it is ready and, if not, why, and
writes a line for every change: the time in milliseconds and `READY`
or the sense data (`2/3A/00` no disc, `2/04/00` not ready, `2/04/01`
becoming ready, `6/28/00` the medium may have changed).  CD EJECT has the drive push
the disc out; take it and push it back in, as often as there is room
on the screen (eleven lines; the log wraps), with the update disc and
with other discs.  NEXT times the drive reading 256 sectors, which is the
number the emulated drive's speed is still waiting for (1.5 ms a sector
is a guess).  SETUP, with the update disc in the drive, hands over
to the stock loader; without a key it does so after ten minutes
(`-DMINUTES=n`).  The lines also go to the debug serial port and to
the CAN bus (identifier 0x7F5: line number, time in three bytes, the
three numbers, and 0 for an error, 1 for ready, 2 for no answer).

    sdk/build.sh 192 drive
    rnse custom disc.iso V220 sdk/out/drive-192.bin drive.iso --index 0660
    rnse loader drive.iso --variant V220 --key CD_EJECT@3000 --disc-back 6000 \
        --key SETUP@16000 --cycles 5000000000        # what the emulator answers

In the emulator the log reads `READY`, `2/3A/00` at the eject,
`2/04/00` when the disc is back, `6/28/00` six seconds later and
`READY` a tenth of a second after that.  A photograph of the real
thing is what the drive's model needs: what the drive says while it
loads (`2/04/00`, as assumed, or `2/04/01`, or still `2/3A/00`), for
how long, and whether `6/28/00` comes before that or after.

### Many questions in one run

`sdk/examples/suite` is for a unit on a bench with a logic analyser on
its CAN wires: ten measurements one after the other, every answer a
record on the bus (frames 0x7F8, sent to nobody like the report's), on
the screen and on the debug serial port.  The processor's speed with
and without the cache, the A/D converter's inputs, the ports and the
registers at 0xb6000000, the clock chip against the timer, the
faceplate controller's start, what the radio processor says on its
line when nobody asks, the drive's name and its reading times at three
places of the disc, the CAN controller's counters, thirty seconds in
which keys and knobs are logged, and at the end the watchdog: the
strobe stops and records follow every 10 ms until the unit is reset,
which is how long the watchdog waits.  It writes nothing anywhere and ends in a restart, so the disc
can be used again.  `tests/disc.rs` runs it against the emulator's own
models; the header of `main.c` has the records.

### What it would settle

Which processor is in the Aisin-marked package
(PVR, PRR); whether the 192's peripheral clock is 58.8 MHz, as the
emulator now has it by analogy with the 193, or 29.4 MHz (the clock
register's last three bits, and the card processors' crystal as a
second witness); the card processors' clock and straps; how the bus and the ports are really set
up when a loader starts; and, with the ROM, everything about the boot
ROM that `docs/boot-rom.md` has had to infer.  A boot ROM image is
firmware like the rest and must not go into the repository.

A unit's report has been taken ("On a real unit"), and its boot ROM
runs in the emulator (`docs/boot-rom.md`, "The real one, in the
emulator").

In the emulator the program reads back the emulator's own stand-ins
(`tests/disc.rs`, `report_program_sends_the_boot_rom`), which proves
the plumbing and nothing about a unit.  The registers of the 192's
list are in the SH7750R's manual and reading them does nothing; that
the processor in the package is one where that holds is likely and
not certain.

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

The drive code works by programmed I/O and polling on both units.  On
the 193 that is how the stock loader reads the disc, and the set-up of
the interface is the loader's, less its enabling of the drive's
interrupt.  On the 192 the stock loader has disc data moved by the DMA
controller and waits for interrupts; programmed I/O through the same
data register is what the stock operating system does for the short
answers to other commands, so reading whole sectors that way goes
beyond what the stock software is seen to do there, if only in length.

The CAN code is each stock loader's set-up, bit timing included (100
kbit/s), with one mailbox receiving everything where the loaders
receive a few identifiers.  Received frames are taken by interrupt, at
the level and through the mask the loaders use, into a queue; sending
waits for the acknowledgement by polling.  The 192's loader does not
reset the controller before it sets it up, so neither does the SDK
there: whoever ran before has left it running.

Things found on the way that a program has to know:

* On the 192 the graphics chip is little-endian on a big-endian bus:
  pixel N of an 8-bit surface is the byte at address N XOR 3.
* The faceplate controller is held in reset until the program releases
  it, and says nothing until then.
* On the 193 a program in the loader's place runs at addresses in P0,
  where stores stay in the data cache; on the 192 it runs in P1, where
  they are written through.  Anything a device is to read from RAM
  (the 193's display surface, a DMA buffer) is written through the
  uncached P2 address of the same memory, or written back with `ocbwb`.
* The installed software leaves DMA channels armed (for the SD card
  processors and the drive on the 192) that go on writing to memory;
  the stock loader switches every channel off first thing, and so does
  the SDK.
* The memory the loader is copied to is not all the loader's.  The
  North American 192 software has variables of its operating system at
  0x8df710f0 to 0x8df71108, 0x710f0 into the loader's place, and
  changes them until the loader has masked interrupts; the stock loader
  has part of a picture there.  Keep code and data low in the place (the
  SDK's are within the first 0x20000 bytes), and expect such things of
  software versions nobody has looked at.
* The stock loader never sets the direction of the ports it drives, so
  the boot ROM has done so before it starts.  A program in the loader's
  place inherits that, like the rest of the boot ROM's set-up (memory
  controller, clocks, the drive).

## What the emulator checks, and `--strict`

The emulator runs the program on its models of the CPU, the timers, the
interrupt controllers, the display hardware and so on.  Those models
were built from the behaviour of the stock software; they are right
where that software exercises them and unknown elsewhere.

`--strict` makes the emulator less forgiving in five ways.  It models
the processor's instruction and data caches, with the cache control
register as the stock operating system of both units sets it
(0x80008181: both on, stores through P1 written through, stores through
the other areas kept in the cache until the line is written back).
Code that is written and then run without the instruction cache being
invalidated runs as the old code; data that is only in the cache is not
what a device or an access through P2 sees; both are reported when the
run ends.  (The organisation is simplified, direct-mapped where the
real caches have two or four ways, so which line is in the cache at a
given moment is not exactly as on the chip: see `src/sh4/cache.rs`.)
It starts
the machine with arbitrary data in RAM instead of zeroes, as after
power-up (the stock software boots either way; a program that forgets
to clear what it uses will not).  It starts a program in the loader's
place with the three timers running and their interrupts enabled and
open in the interrupt controller: both stock loaders begin by stopping
every timer and clearing the controller, which they would hardly do if
the boot ROM left those alone, and a program that enables interrupts
without doing the same gets interrupts it has no handler for.  It lets
the watchdog bite from the first moment, not only once the program has
begun to strobe it, and says so when the strobe was ever left alone for
longer than the stock software leaves it.  And it lists, when the run
ends, every address the program used that no model answers for:

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
  toggling the strobe, 105 ms without an edge reset the unit.  That is
  a 192's, measured: with the strobe left alone the unit was reset
  between 105 and 115 ms after its last edge.  (The emulator had 1.5 s
  until then.)  The 193's is taken to be the same.  Interrupts off for
  a tenth of a second is all it takes: copying half a megabyte through
  uncached addresses, as the hand-over does, takes 107 ms on a unit,
  which is why that code strobes as it goes;
* interrupts only when unmasked in the controllers, with the stock
  priorities;
* the CAN controller sends and receives only out of its reset and halt
  states, and a frame it sends takes its time on the bus (1.2 ms for
  eight bytes at 100 kbit/s) before it counts as sent; `--strict` says when frames went out with other bit timing
  than the stock software's, and `--can-alone` leaves every frame
  unacknowledged, as on a bench;
* serial ports send only with the transmitter on and hear only with the
  receiver on and the FIFO out of reset; the faceplate controller is
  silent until its reset line is released.

## On a real unit

One unit so far: a North American 192 (V222) with software index 0260,
on a bench, alone on its CAN bus, with a logic analyser on the bus and
on eight test pads, and the update disc with V222 software 0371.  What
happened, in order, because the failures say more than the success:

1. **The probe, as it then was.**  The stock software took the disc
   (index 0300) and started the program.  The screen went dark and
   stayed so; a minute and a half later the unit reset and started as
   usual, with the old index.  The disc came out on request: after a
   start the stock software needs some 40 s before it starts the
   program again, and it obeys the eject key until then.  So on a bench
   the trap of "The way back" has a door.
2. **Why dark.**  `board_init` wrote the latch at 0x16000004 with the
   value the stock loader's write has in the emulator.  The loader adds
   a bit to what is there; the value took the faceplate controller's
   reset line down with it, and the lighting is that controller's
   (`docs/hardware.md`, "The latch at 0x16000004").  The picture itself
   was there all along: started again (`panel_init`), the controller
   lit it.
3. **Why no update.**  The hand-over worked from the first disc on: the
   stock loader ran, 90.6 s each time, without reading the disc, and
   reset the unit.  The same latch: with `board_init` adding its bit
   and leaving the rest, the loader that the program starts does the
   whole update, in the 148 s it also takes when the stock software
   starts it.
4. **What works now** (`sdk/examples/beacon`, the SDK as it is): the
   stock software starts the program; the picture matches the emulator's
   as far as a photograph shows; the tick is right (CAN
   frames a hundred ticks apart were 1.00003 s apart); the faceplate
   controller takes its settings within a second; the CAN controller
   sends; `stock_update` hands over (the stub needs 4.9 ms for the
   caches and 107 to 112 ms for the copy, uncached and with interrupts
   off, which is why it now puts edges on the watchdog's strobe) and
   the unit updates itself from the disc and has the disc's index.

Things that were wrong in what had been assumed, besides the latch:

* A unit alone on a bench lets its CAN bus sleep after 4.7 s, and from
  then on the transceiver passes nothing.  The beacon wakes it (two
  port bits) before anything else; those two lines are on test pads,
  so their rising is the moment a program starts, and the hand-over
  shows its three steps on them.
* The debug serial port is on none of the eight pads looked at.
* Two beacons with the old `board_init` ended in a reset within a
  fraction of a second of the hand-over, where the probe before them
  and everything after handed over.  That was the watchdog, as good as
  certainly: it waits 105 to 115 ms (measured since), the hand-over's
  copy with interrupts off takes 107 to 112, and the stub has strobed
  as it copies since the disc after those two.
* `can_send` gave a frame up after 100 ms, and the next frame, asked
  for at once, did not go out: the controller cancels when the attempt
  it is making is over.  `can_send` now waits for that, and the
  report's stream, sent to nobody at 3 ms a frame, arrived whole.

5. **The report** (`sdk/examples/report`, three rounds of pages).  It
   ran through, card processors included, and handed over; the update
   followed.  Nobody acknowledged, so it sent each frame for 3 ms and
   went on (`can_send_for`); a logic analyser on the bus had 6.8 rounds
   of the stream in the 265 s, some 3.9 ms a frame, and `rnse report`
   put them together: all 104 registers, and the boot ROM's 49152
   bytes with the check sum the unit computed itself.  Among the
   answers: PVR 0x04050086, PRR 0x00000200, CVR 0x20480000 (the
   emulator answers 0x04050005, 0x100, 0x35100000); the clock register
   0x0e0a, as assumed; each card processor's crystal loop in 21846327
   counts of timer 0, which makes the peripheral clock 58.803 MHz; the
   card processors' pins 0x3f, multiplier 3, the second loop 3.000
   times as fast, all as the emulator has them, and their clock
   control register 0x0100 after a reset where the emulator has 0.
   The same program in the emulator, under the unit's boot ROM, now
   reports the same in all but a handful of its 104 values: the levels
   of the two CAN transceiver lines and of the watchdog's strobe, which
   depend on the moment; a register of the clock chip (0x09 on the
   unit, 0 here); and the card processors' two times, which are
   0.0035 % longer on the unit, its crystals being what they are.  It
   took the processor's identification, a few reset values (IPRD,
   INTMSK00, timers 3 and 4), a DMA control bit that reads as 0, the
   card processors' clock control register (0x0100 after a reset) and
   the idle levels of the port pins to get there.

6. **The drive's log, ending in a restart** (`sdk/examples/drive`
   built with `-DRESTART`).  The keys answer (CD EJECT, RETURN), the
   drive takes commands from a program and its answers are logged
   (`docs/boot-rom.md`, "A disc that goes in while the unit runs", has
   them), and `restart()` works: the unit started again, unchanged,
   and 71 s later its software had started the program from the disc
   once more, by itself.  The screen was lit from the start: with the
   latch left alone the faceplate controller keeps its lighting, and
   `panel_init` after it does no harm.

7. **The same with a reading test.**  256 sectors read by the SDK's
   own loop in 1510 ms.  After an eject and a disc pushed back in,
   reading failed: the drive does not turn a disc it has loaded until
   it is told to start, which the stock software does and the SDK did
   not (`docs/boot-rom.md`).  `cd_wait_ready` sends the command now,
   as the stock software does, whenever the drive is not ready with a
   disc in it; that has run in the emulator only.  The command itself
   works from a program: sent by a key of the drive's log, it had the
   drive ready 5.65 s later.

8. **The suite** (`sdk/examples/suite`): ten measurements in 39 s, all
   of them answered, 123 records on the bus.  What the emulator took
   from it: the watchdog waits 105 to 115 ms, not 1.5 s; the drive
   calls itself `ALPINE  DV33M01B/`, reads 256 sectors in a row in
   1342 ms and needs 200 to 280 ms to get somewhere else on a small
   disc; the A/D converter's idle inputs; the processor's clock chip
   is not running; the faceplate controller has its settings 340 ms
   after its start.  What it could only note: the processor is a sixth
   faster than the emulator's in a loop and four times slower past the
   cache (`docs/hardware.md`, "CPU"), and the radio processor's status
   block as it really is.  Every key and both knobs answered, with the
   SDK's numbers for them.

9. **The suite, eighteen tests.**  `cd_wait_ready` after a disc change:
   ready 7.7 s after the disc was back, 64 sectors read after it.  The
   flash chips' own account of themselves (`docs/hardware.md`, "The
   boards"), which gives the times an erase and a write take without
   erasing anything.  Only three serial channels are in use.  Reading
   8192 words takes 1.06 ms from the flash through the cache, 1.40 ms
   past it and 6.0 ms from the battery-backed memory, which is slow.
   A card pushed into the first slot shows as bit 6 of port 2, as the
   emulator has it; the display open reads 47 on A/D input 3 and 0 on
   bit 2 of port 3, likewise.  The first 60 KiB of the battery-backed
   memory came out before a key cut it short.  The radio processor
   did not answer the two questions, which were put to a receiver that
   had stopped at an overrun: the program clears that now.

10. **The suite once more**, with prompts that can be seen.  The radio
    processor's version and its status block (`docs/hardware.md`,
    "Radio/audio processor"); the second card slot's switch, bit 0 of
    port 6, as the emulator has it; and the whole of the battery-backed
    memory but for some 350 pieces of five bytes that were lost on the
    way.  `rnse boot --nvram FILE` starts the emulator with it: the
    installed software then finds its settings as that unit left them
    instead of an empty memory to initialise.  Such an image is as
    private as the unit's owner: destinations, the car's number.

Not tried on the unit: receiving on CAN.

## What this does and does not establish

Established, by the tests in `tests/disc.rs`: the examples build and
behave as written on both emulated units; and the stock software of
both units, European and North American, running in the emulator,
takes a disc made by `rnse custom` with a higher index for an update,
passes it through its checks (project code, checksums of every module
on the disc) and starts the program, which then runs correctly from the
state that software leaves and writes nothing to flash.

Not established:

* **That every real unit gets as far as the emulated one.**  One North
  American 192 does ("On a real unit"): switched on, showing the radio,
  no ignition, nothing else on the bus.  For the European 192 and the
  193 the decision
  to start the loader is the stock software's own code, running
  unmodified, so that part is as good as the emulation of what it needs
  on the way: the drive, the interrupt and DMA controllers, the boot
  ROM services.  A real unit may differ in ways this never touched.  And what
  the stock software has put in the unit's battery-backed memory over
  the years is not what the emulator starts with.
* **That the bring-up is sufficient.**  It is the stock loader's, minus
  the parts the example has no use for (the drive, CAN, DMA).  On the
  192 it has shown its picture on a unit.  The lesson of that unit
  applies to the rest of both hardware layers, the 193's above all,
  which nobody has checked for it: a write recorded from a loader
  running on its own in the emulator may be a read and a write on a
  unit, and what it leaves alone there matters.
* **Timing.**  The emulated CPU executes one instruction per clock
  cycle at an estimated clock (`--cpu-ratio` tries others); the
  watchdog's real period and the display's real requirements are
  unknown.  The timers' clock is known on the 193, where the unit's own
  sound output measures it (50 MHz, a tick of 1 ms for the stock
  software); on the 192 it is 58.8 MHz, as the two units' software
  having the same timer values in the ratio of their clocks suggested
  and a unit has since confirmed.  The emulator had both at half that until the sound
  output was modelled, and the SDK's "10 ms" tick was 5 ms then; it is
  10 ms by the corrected clocks.
* **The faceplate in the loader's place.**  The link's set-up is the
  operating system's, but the operating system runs after the boot ROM
  has started every module; whether the pins of that serial port and
  the reset line are already switched to those functions when a loader
  runs is not known.  The emulated controller is a stand-in that knows
  the frames the stock software exchanges with it; how long the real
  one takes to start, and whether it insists on every setting, is not
  known either.
* **The drive's timing.**  The emulated drive answers at once and has
  the disc spinning.  That a drive first reports a changed disc and
  wants to be asked for its sense data is exercised (the test swaps the
  disc before the program's first command); waiting for a drive that is
  busy or spinning up is not.
* **The caches exactly.**  The cache model has the rules right and the
  organisation simplified.  A program that breaks a rule can get away
  with it here and not on the chip, or the other way round, depending
  on which lines happen to be in the cache.
* **CAN on a real bus.**  The emulated bus delivers whole frames, one
  after the other with the time each takes at 100 kbit/s, and
  acknowledges at once.  Bit timing is copied, not checked; error
  states (error passive, bus off) are not modelled.  A unit that is to
  stay awake in a car also has to take part in the bus's network
  management, which the SDK does not do.
* **Everything else the stock loader does not use.**  Sound, the radio
  processor, GPS: no stock loader touches them, and the SDK has no code
  for them yet.

If you do try it on a unit, try the probe first:

    sdk/build.sh 192 probe              # or 193
    rnse info disc.iso                  # the disc's index for your variant
    rnse custom disc.iso V220 sdk/out/probe-192.bin probe.iso --index 0660
    rnse boot probe.iso --variant V220 --ignition --installed-index 0650 \
        --cpu-ratio 5 --cycles 30000000000 --shot 30000:probe.png   # the whole circle, emulated

(`--cpu-ratio 5` is for the European 192 only: at the emulator's
default pace its stock software's start of a loader runs into the
collision described below.)

with the update disc for exactly that kind of unit, of the software the
unit has (or newer), and an index higher than the unit's in its second
and third digit.  Keep the unmodified update disc at hand.  What should
happen: some time after the disc goes in (the installed software first
adds up every module on it), the probe's picture with a count-down from
60, then the stock update's own screens, then the unit as it was, with
the disc's index.  Other outcomes:

* on a European 192 (V220), the unit starts again once or a few times
  before the picture comes.  The stock software's start of a loader
  can fail by itself there, about one time in six by the emulator's
  numbers, whatever the loader is: one of its own timer handlers runs
  into the loader's image ("Starting the loader" in
  [boot-rom.md](boot-rom.md)).  What a unit does then is the boot
  ROM's business and not known; a restart and another try is the
  likely thing, and nothing has been written anywhere by then;
* nothing, and the unit carries on as usual: it did not take the disc
  for an update (index not higher, wrong variant, or a check the
  emulator does not know about).  The disc comes out as usual;
* the unit restarts again and again without ever showing the picture:
  the program was started and failed, and the watchdog resets the unit
  each time.  This
  is the trap described above.  The program writes nothing to flash
  and the installed software records nothing about the update before it
  starts the loader, so the software in the unit is intact, but getting
  the disc out may take opening the unit;
* the picture, but no update after the count-down: the same trap, with
  the hand-over as the part that failed.

The debug serial port is a header inside
the unit; its location is not known.  On the 193 the divisor the stock
loader gives it, which the SDK copies, makes 57600 bit/s from the 50
MHz peripheral clock.  The 192's parts are known from
photographs of an opened unit (`docs/hardware.md`, "The boards").  What
would help most now is a report of what the screen shows, the same
photographs of a 193, and the contents of the boot ROM: the first
48 KiB of the flash chip at address 0, which a program started this
way could read and send out.
