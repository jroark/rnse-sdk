# Writing your own software for the unit

`sdk/` holds what is needed to write a program from scratch, run it in
the emulator and put it on a disc for a real unit: start-up code, a
small hardware layer for each generation of the unit, and an example.

Read the section "What this does and does not establish" before
taking a disc to a car.  Nothing here has been run on real hardware.

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
`SOFIDX` of the update it last took (presumably the software number its
version display shows, which has not been checked).  The same disc with an index that is not higher is
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

The emulator runs the whole circle, with the stock software at both
ends (`rnse boot probe.iso --installed-index ...`, and the test
`custom_program_hands_back_to_the_stock_loader`), and with the
processor's caches modelled (`--strict`, and always in that test).  The
hand-over replaces running code, which is where caches bite: the stub
purges the data cache over the place, invalidates the instruction cache
and copies through uncached addresses.  Take the invalidation out and
the 192 fails in the emulator as it would on the bench, executing what
is left of the program from the instruction cache.

## Building and running

You need an SH-4 GCC that can produce big-endian code without its
libraries; Debian's and Ubuntu's `gcc-sh4-linux-gnu` will do.

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
CAN bus (`can_init`, `can_send`, `can_receive`).  Six examples
(`sdk/examples/report` has a section of its own below):
`sdk/examples/probe` is the one for a first try on a unit (see "The way
back"); `sdk/examples/hello` draws a test picture and counts seconds;
`sdk/examples/keys` shows what the faceplate reports (`sdk/build.sh 193
keys`, and `--key SETUP@1500 --knob 1:3@2500` on the `rnse loader`
command line to press and turn); `sdk/examples/cd` reads the disc the
program came on; `sdk/examples/can` lists the frames on the bus and
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
on no disc.  Then it shows all of it and sends all of it:

* on the screen, page after page, twelve lines of address and value to
  a page, then the state it was started in (SR, VBR, where its caller
  was, the stack pointer, the arguments) and check sums of the boot
  ROM, the whole and each 4 KiB of it.  Photographs of the pages are a
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

What it would settle: which processor is in the Aisin-marked package
(PVR, PRR); whether the 192's peripheral clock is 58.8 MHz, as the
emulator now has it by analogy with the 193, or 29.4 MHz (the clock
register's last three bits); how the bus and the ports are really set
up when a loader starts; and, with the ROM, everything about the boot
ROM that `docs/boot-rom.md` has had to infer.  A boot ROM image is
firmware like the rest and must not go into the repository.

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
  toggling the strobe, 1.5 s without an edge resets the unit.  The real
  limit is not known.  What is known is what the stock software does:
  an edge every 10 ms from its tick, and never more than about 220 ms
  between two (192) or 135 ms (193), from the first instruction on.  A
  program that keeps to that is as safe as the stock software;
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

## What this does and does not establish

Established, by the tests in `tests/disc.rs`: the examples build and
behave as written on both emulated units; and the stock software of
both units, European and North American, running in the emulator,
takes a disc made by `rnse custom` with a higher index for an update,
passes it through its checks (project code, checksums of every module
on the disc) and starts the program, which then runs correctly from the
state that software leaves and writes nothing to flash.

Not established:

* **That a real unit gets as far as the emulated one.**  The decision
  to start the loader is the stock software's own code, running
  unmodified, so that part is as good as the emulation of what it needs
  on the way: the drive, the interrupt and DMA controllers, the boot
  ROM services.  A real unit may differ in ways this never touched; it
  might, say, need the ignition on or a particular screen.  And what
  the stock software has put in the unit's battery-backed memory over
  the years is not what the emulator starts with.
* **That the bring-up is sufficient.**  It is the stock loader's, minus
  the parts the example has no use for (the drive, CAN, DMA).  If the
  picture needs one of those, or needs the loader's exact timing, the
  emulator will not show it.
* **Timing.**  The emulated CPU executes one instruction per clock
  cycle at an estimated clock (`--cpu-ratio` tries others); the
  watchdog's real period and the display's real requirements are
  unknown.  The timers' clock is known on the 193, where the unit's own
  sound output measures it (50 MHz, a tick of 1 ms for the stock
  software); on the 192 it follows from the two units' software having
  the same timer values in the ratio of their clocks (58.8 MHz) and is
  not measured.  The emulator had both at half that until the sound
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
