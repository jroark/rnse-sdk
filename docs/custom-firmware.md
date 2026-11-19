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
seconds).  The disc sits behind the motorised display, which only a
program that runs the motor itself can fold down (`display_open`, see
"What a program gets"; the eject key is behind it too).  So a program
that just runs is a trap: the unit is not damaged, but it is the
program's until the disc is out, and the disc does not come out.  A
program that offers the display key is less of one, but one that hangs
or fails before it does is the same trap, which is why the way back
below does not depend on it.

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
custom` put it, so the program's `stock_update` works. The loader saves
its RAM image before starting the test. Returning from `main()` or calling
`loader_return(status)` restores the loader directly, without restarting
stock firmware. It waits for card removal and reinsertion; RETURN reruns
an inserted card. See [the persistent loader instructions](sd-loader.md)
for the return contract, reserved memory, and build commands.

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

Before direct return was added, the original restart-based path was tested
in the emulator in both slots, with the stock software and
a unit's boot ROM at both ends (`sdk/tests/emulated.rs`,
`programs_come_from_an_sd_card`).  On a unit (a North American 192
with software 0371, a 32 MB card formatted FAT16 by macOS, in either
slot) it runs: the card processor takes its program, the card starts in
4.0 s, the file is found and read in 40 ms, the program runs from it
with its picture on the screen, restarts the unit when it is done, and
the stock software starts the loader again 46 s later: 62.7 s a round
with a program that shows itself for ten seconds, round after round
with nobody touching the unit.

A 512 MB card did not start on that unit, in the first slot, twice: no
answer to the first application command (CMD55) after CMD8 went
unanswered as well, where the 32 MB card answers.  The stock software
reads that card (it shows its 485 MB).  What the stock driver does
with such a card the emulator shows, with a card made to behave so
(`--sd-deaf N`, `--sd-mmc`): it starts the card anew (CMD0) and asks
CMD1, a MultiMediaCard's question; a card that answers is started as
one (CMD1 every 300 ms until ready, and the address 0 for it); with no
answer it asks CMD55 again, and so round and round.  `sd_init` does
the same since.  Which of the two that card is, slow to wake or a
MultiMediaCard, is not known: on the unit that card answered that
round no better (six starts, neither CMD55 nor CMD1 answered).

So there is a second way to a card, `sd_attach`: the card as the
installed software left it.  That software has sent the card
processor its program and started the card before it ever starts a
loader, and both are still so; `sd_attach` resets nothing, throws away
what the processor still had to say and asks for a sector, and finds
out by trying whether the card counts in bytes or sectors.  `sdboot`
tries that first and `sd_init` after it.  In the emulator it reads
standard and high-capacity cards so, in 40 ms.  On the unit it read
that 512 MB card: 30 ms for the card, 50 ms for the file, and a round
of that restart-based loop is 56.8 s with it, round after round.  It wants the card
in its slot before the disc goes in, so that the stock software has
started it.

That left the 512 MB card unreadable once it had been out of its slot,
or after Quake: the persistent loader could not attach it, and
`sd_init` answered -7.  What was wrong showed when the loader put the
words of a failed start on the screen: the card is of version 2 and
answers CMD8, the first CMD55 and the first ACMD41 (busy, as cards are
at first), and then nothing more.  The notes above, "CMD8 unanswered",
were mistaken.  Between two rounds of ACMD41 the driver set the card
interface's register 0x3814 again, which is what the stock driver does
for a card that leaves CMD8 unanswered (the 32 MB card); for one that
answers it the stock driver does not, and asks ACMD41 a second time
without CMD55 instead (a card takes that for an illegal command and
does not answer), every step 150 ms apart.  The emulator's cards were
ready at their first ACMD41, so that loop had never run in it; they are
busy for three rounds now, as real ones are.  The driver follows that
loop since.  The stock software also never resets a card processor for
a card that goes in while it runs; it starts the card on the program as
it runs, and says `0200` to the processor when a card goes
([hardware.md](hardware.md), "Messages").  `sd_restart` and
`sd_card_out` do that, and `sdboot` opens a card by `sd_attach` (not
for a card it saw go in), then `sd_restart`, then `sd_init`; when all
fail it shows each answer and the first 48 words of the restart (CAN:
items 4, 6, 8 + slot and 0x40 on).  A program's return to the loader
waits for the end of an exchange with the processor (`sd_busy`); in the
emulator the processor's program drops half a message by itself, so
this is a precaution.  Not yet run on a unit.

None of that was what stopped the 512 MB card, as `linkbench` showed
on the unit the same day (its card tests, run with the RP2040 bridge in
the other slot carrying the program and the results:
[sdbridge.md](../sdbridge/docs/sdbridge.md)).  With the words of `sd_init`'s exchange
kept, the card went through its busy rounds and answered its last
ACMD41 ready; what went unanswered was the CMD55 after that, the step
"once more without the claim" that the driver asks of a standard card
that answered CMD8.  A card that is ready has no application commands
until it has an address, so it says nothing, and the driver gave up
with -7.  The stock driver asks the same and gets the same silence,
and then starts the card anew (CMD0, CMD1) and goes round again as for
a card that left CMD8 unanswered, which the card takes; `sd_init` does
that since, and started the card on the unit.  The emulator's card had
answered CMD55 in any state, so the step never failed there; it answers
nothing to CMD55 in the ready and identification states now.  (Bit 2 of
the card interface's SDCTL, which `linkbench` found holding back the
response interrupt, was not it: the installed program takes it down
itself, by reading SDCTL after each response's words.)

Three things a unit taught on the way:

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

What has not run on a unit: a high-capacity card, FAT12 and FAT32 (in the emulator each reads a card
image formatted by macOS).

A Linux kernel comes from a card the same way: `sdk/examples/linuxboot`
as `RNSE.BIN` reads `VMLINUX.BIN` from the card and starts it.  See
[linux.md](linux.md).  Doom too: `sdk/examples/doomboot` as `DOOM.BIN`
reads the game and the shareware WAD from the card and starts it.  See
[doom.md](doom.md).  And Quake: `sdk/examples/quakeboot` as `QUAKE.BIN`
starts the game, which reads its data from the card as it needs it
(`sd_open` walks a file's chain of clusters once, `sd_pread` reads any
part of it), see [quake.md](quake.md).

### Sound

`sd_play` has the card processor play an MP3 file of the card: the
processor's program is the stock software's player, and it is told
where the file's bytes are as the stock driver tells it
(`docs/hardware.md`, "Messages").  `sd_play_poll` keeps it fed.  The
sound comes out of the processor's converter, and whether that is
heard is the radio processor's business, which has the amplifier:
`radio_source(RADIO_DECODER, volume)` and `radio_volume`
(`sdk/src/radio.c`, the frames the stock software sends for the
same).  `sdk/examples/player` is a player made of the two with the
faceplate's keys and knobs.  In the emulator it plays the test file's
two tones out of the speaker at the right pitch on the right sides
and the volume knob moves the amplifier (`sdk/tests/emulated.rs`,
`sdk_player_plays_an_mp3`).  Standard cards only.

On a unit (the North American 192, the player from a 32 MB card, a
speaker on the front right output) it plays: the file's tone is heard
and the volume knob makes it louder and quieter.  The card processor
plays the test file through in 6.06 s and the next after it; NEXT,
PREV, SELECT and the select knob start files as fast as they are
pressed; the radio processor acknowledges the source and every volume
from 0 to 30.  So the two frames are all the amplifier wants, from
the state the stock software leaves it in.

The first try on the unit failed three seconds into the file, with the
screen dark for the time: the driver waited to write a word while the
card processor waited for one of its own to be taken, and with the
driver stuck nobody answered the faceplate controller, which has the
light.  The stock driver takes the processor's words in an interrupt.
The SDK's now takes what is there to read whenever it waits to write
(`put` in `sdk/src/sd.c`), and that was all it wanted.

The tuner is the radio processor's too: `radio_tune` sends it to a
frequency, and `sdk/examples/tuner` is a radio made of that, the
source and the volume.  On the unit it is heard, and tunes: the radio
processor acknowledges each frequency.

`sdk/examples/timing` is a row of measurements made to come from a
card: the display's vertical syncs, what a sector from each card takes,
and the time of an access to each part of the bus.  No hands wanted; it
restarts the unit when it is done.

## A program in flash

Everything above is a program that a disc or a card brings each time.
`rnse firmware disc.iso V222 program.bin out.iso --index NNNN` makes an
update disc of a program instead: the disc's operating system module
is replaced by one of the same length that holds the program behind a
few instructions (`fw::firmware_module`), everything else on the disc
is as it was.  The unit's software takes the disc for an update, its
loader writes the module where the operating system was, and from then
on the boot ROM, which starts whatever is there with a right checksum
(`docs/boot-rom.md`), starts the program: the instructions copy it to
the loader's place in RAM, where every program of the SDK runs, and
jump to it.  No disc, no card, at every start.  The 192 only.

The stock software is gone then, except for what the program does not
replace: the other modules, and the operating system's module from its
second megabyte on.  The card processors' program is in that part (at
0x200000 of the flash), which is where `sd_init` finds it; the first
firmware module that was made had zeroes there, and in the emulator a
program from flash then found no card.  Three ways lead back, and all
three end in the boot ROM installing the update disc that is in the
drive:

* `firmware_remove()` and `restart()`: the program erases the 8 KiB
  sector with the directory of the installed software, and the ROM,
  finding no software, reads the loader from the disc and runs it.
  `sdk/examples/installed` does it when RETURN is held.
* The guard (`sdk/src/flash.c`, called by the start-up code before
  `main`).  A program in flash counts its starts in the battery-backed
  memory (at 0x3ff00, above all the stock software uses) and sets the
  count back when it calls `firmware_healthy()`.  The fifth start in a
  row that did not get that far takes the program out as above.  That
  is for a program that hangs or dies: nothing in the ROM would take
  it away (a program that hangs is reset by the watchdog and started
  again, 1276 times in three minutes of the emulator).
* By hand, through the same guard: off and on four times, each time
  before the program calls itself healthy (ten seconds in, in the
  example), is the way out of a program that runs and cannot be
  operated.

**The disc that installed the program must not be what the ROM
installs from.**  A disc has its loader twice: in the container, where
the installed software takes it from for an update, and as a file
(`LDM.BIN`), which is the one the boot ROM runs when the unit has no
software (tried with each replaced alone: each runs its own).  If both
were the stock loader, a unit whose program was taken out with that
disc still in the drive would get the program back at once, and a
program that does not work would never let the disc go, since nothing
but the program could push it out.  So `rnse firmware --swap swap.bin`
puts a program of the SDK in the file's place and leaves the stock
loader in the container: `sdk/examples/swap` pushes the disc out, says
what to put in, and restarts the unit when a disc has gone in and is
ready.  Always make a firmware disc with it.

In the emulator, under a unit's boot ROM and with arbitrary RAM and the
caches modelled, all of it runs (`sdk/tests/emulated.rs`,
`a_program_is_installed_in_flash_and_taken_out_again`,
`a_firmware_disc_left_in_the_drive_does_not_install_again` and
`a_program_in_flash_that_never_gets_going_takes_itself_out`): the
install through the stock software and its loader, the program from
flash with its picture and the faceplate answering, the way out by the
key, and the guard; afterwards the flash holds the stock software byte
for byte and it starts.

**On a unit** (the North American 192, software 0371) the circle has
been walked once, with `sdk/examples/installed`:

* The stock software took the firmware disc for an update and its
  loader installed it; 150 s after the software's start the unit was
  reset and the boot ROM started the program from flash, with its
  picture, the faceplate answering and the keys working.
* CD EJECT pushed the disc out.
* RETURN, held: the directory's sector was erased (the erase on real
  chips, which the emulator's flash could not vouch for) and the unit
  restarted, 32.8 s after the program's start.
* **The boot ROM, finding no software and no disc, asked for one on
  the screen**, and when the stock update disc went in it installed
  it and started it: 169 s from the restart to the reset that ends the
  update.  So a real boot ROM does install from a disc by itself,
  which every way back rests on, and it waits for the disc.

Installed a second time, the program also started after the power had
been cut: the unit comes up into it by itself.

What has still not happened on a unit: the guard's count running
to five, and the swap loader, since the disc was out when the program
went.  (A program from flash finds the radio processor and the card
processors as the boot ROM leaves them and not as the stock software
does; `sdk/examples/base`, below, has used both so.)

This is still the one part of the SDK that can leave a unit without
software: keep a stock update disc with the unit.

`sdk/examples/base` is a firmware of that kind with something to do: a
radio and an MP3 player, with the way out.  It brings up what the stock
software would have, since from flash nothing has: `radio_start` sets
the line to the radio processor and sends the frames the stock software
sends at every start, in its order, and `sd_init` starts a card from
nothing when MEDIA is first pressed (with `sd_idle` set to
`panel_poll`, so that the faceplate controller is answered through the
seconds that takes).  In the emulator, started by a unit's boot ROM, it
tunes, plays a file from a card and goes back
(`a_firmware_from_flash_is_a_radio_and_a_player`); and on a unit, installed in flash, the card half works: a card was
started from nothing and its file played through.  The radio
processor answered nothing, not one of nineteen frames, and so there
was no sound and every turn of a knob waited two seconds for an answer
that did not come.  The line's format was wrong: `radio_start` had
set the faceplate's (mode 0x0011, with a parity bit), and the radio
processor's is mode 0, eight bits and no parity, which is what the
boot ROM and the stock software both write (seen in the emulator's log
of their register writes, which is where it should have been looked up
in the first place).  A program from a card never noticed, the line
being set already.  The emulator's stand-in took any format; it hears
only mode 0 now.

With the line right the whole of it works on a unit, installed in
flash and after a power cut too: the radio is heard within three
seconds of power, the knobs answer, a card is started from nothing and
its file played.  What the radio processor does at a start is not what
the stand-in does, though.  It refuses frames: of the nineteen that
`radio_start` sends, eight came back with an acknowledgement of 0x07
(and one, `d0`, of 0x08) and were taken when sent again 100 ms later,
as the first frame after them was at other starts; the driver took a refusal for
no answer at first, and the screen said so over a playing radio.
`radio_command` now sends a refused frame again, for two seconds.  And
one frame, `8a`, a unit's radio processor says nothing to at all: it is
sent and not waited for.  What 0x07 and 0x08 mean (busy with the frame
before, presumably: the frames are sent back to back, which the stock
software may not do) and what `8a` is for are not known.

## Building and running

You need an SH-4 GCC that can produce big-endian code without its
libraries; Debian's and Ubuntu's `gcc-sh4-linux-gnu` will do.  Where
there is none to install (macOS), `sdk/tools/sh4-docker` runs that one in a
container: `PATH=$PWD/sdk/tools/sh4-docker:$PATH sdk/build.sh 192`.

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
palette (`screen_pixel`, `screen_fill`, `screen_text`, `screen_blit` for
a whole picture, a word at a time on the 192), the way back to
the stock software (`stock_update`, above), and the keys and
knobs of the faceplate (`panel_init`, `panel_poll`, `panel_key`,
`panel_knob`; `panel_pressed` for a press that came and went while the
program was busy), and the CD drive (`cd_init`, `cd_wait_ready`, `cd_read`,
`cd_find` for a file in the root directory of the disc), and the car's
CAN bus (`can_init`, `can_send`, `can_receive`), and the fold-down
display in front of the drive and the card slots (`display_open`,
`display_close`, `display_follow_key` for the stock software's key
next to the power button; `display_position`, `display_state`).  The
tick stops the display's motor when the position no longer changes,
as the stock software does (`docs/hardware.md`, "A/D converter and the
fold-down display"), and every hand-over to another program stops it
at once.  `panel_service()` is what every program owes the keys
wherever it waits: the faceplate answered, the display key followed
and CD EJECT obeyed (`cd_eject`), so that the disc and the cards can
always be got at; set `sd_idle` and `display_idle` to it as well.
`sdk/examples/display` shows the position and moves the display (the
key, NEXT, PREV, SELECT to stop; RETURN held for two seconds back to
the SD loader); `sdk/examples/sdboot` and `sdk/examples/base` follow
both keys.  Seven examples
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
| display motor | port 3 (0x12600001) bit 3 open, 4 closed; A/D input 3 | port 0xff93f008 bits 18, 19 (which is which assumed); A/D input 4 |

The program keeps to the memory the stock loader's image occupies.  It
uses no service of the boot ROM.

Sound, on the 192, is behind the two signal processors of the card
board, like the SD cards: see "Programs from an SD card", "Sound".
Samples of a program's own go out of a processor's converter with
`pcm_start` and `pcm_put` or `pcm_dma`, and without gaps with
`pcm_stream`, a ring of blocks the DMA controller plays one after the
other, each started from the last one's interrupt (Doom's sound,
[doom.md](doom.md)), or as a sound card would, with `pcm_loop`: the
ring played round and round with no block to wait for, and
`pcm_loop_pos` where the controller is in it (Quake's sound,
[quake.md](quake.md)).

A program bigger than the loader's place is loaded by one in it and
entered with `image_enter` (`sdk/src/enter.c`: everything stopped, the
caches seen to); it goes back to the SD loader through the first one's
`loader_return`, which it sets as `loader_return_via`
(`sdk/src/wayback.c`).  `linuxboot` does the first half for Linux,
`doomboot` both for Doom, `quakeboot` both for Quake.

There is no C library.  What the compiler calls on its own is in
`sdk/src/libgcc.S` (division, and the block copy it uses for large
structures; the SH-4 cross compilers that are easy to get are
little-endian, so their libgcc cannot be linked), so `/` and `%` work;
`udiv` and `umod` are still there.  `memset` and `memcpy` move a long
word at a time where they can.  `malloc` hands out pieces of a block of
RAM the program gives to `heap_init` (`sdk/src/heap.c`) and `free` gives
nothing back.  Floating point is switched off (`-m4-nofpu`).

An example may build some of its files otherwise: a file `flags` beside
its sources, a line for each such file, its name and the options that
replace `-m4-nofpu -Os` (`sdk/examples/dvd/flags`: `-O2` for the
decoders, `-m4` for those in floating point).  This GCC's `-m4` takes
FPSCR.PR to be set (double precision) between functions and clears it
inside those that work in single precision, so a program with such
code sets FPSCR to 0x000c0001 before any of it runs (the stock software
leaves 0x00040001); the kit's interrupt code does not touch the FPU.
`font_glyph` gives the 5x7 font's rows for drawing text elsewhere than
on the kit's own surface, and `pcm_dma` takes samples through P2
(uncached), which it then need not write back: that is how
`sdk/examples/dvd` feeds one transfer of 47 s that it writes ahead of.

Code from elsewhere goes in `sdk/third_party/NAME/`, with its licence;
an example that uses it names it in a file `libs` beside its sources,
and `build.sh` compiles every `.c` of it with `-O2` and the few C
library headers in `sdk/third_party/libc`.  So far there is one,
`h264bsd`, an H.264 decoder (see `sdbridge/docs/android-auto.md`).

A program that computes much should set the caches up for itself:
the stock setting the program finds (CCR 0x80008181) writes every store
through P1, where the program, its data and its stack are, straight to
memory, and gives code and data within one 32 MB half of each cache.
`cache_setup(CCR_EMODE | CCR_ICE | CCR_OCE | CCR_CB)` (`sdk/src/cache.S`)
writes the operand cache back and changes that safely; on the 192 it
made the H.264 decoder of `sdk/examples/h264bench` two to three times
as fast (`sdbridge/docs/android-auto.md`).  With P1 copy-back, data the program
hands to a device by DMA has to be written back first (`ocbwb`), as it
already must through P0.

`rnse boot ... --profile FILE` samples where the CPU is every 20,000
cycles; `tools/profile.py sdk/out/NAME-192.elf FILE` adds the samples up
by function.  It counts instructions, as the emulator does, not time on
a unit.

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
which keys and knobs are logged, the display folded down by the SDK's
driver with its position every 50 ms (how long the motor runs, where
its ends read; it stays open for the disc change, whose CD EJECT is
behind it, and closes after), and at the end the watchdog: the
strobe stops and records follow every 10 ms until the unit is reset,
which is how long the watchdog waits.  It writes nothing anywhere and ends in a restart, so the disc
can be used again.  `sdk/tests/emulated.rs` runs it against the emulator's own
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
(`sdk/tests/emulated.rs`, `report_program_sends_the_boot_rom`), which proves
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

Established, by the tests in `sdk/tests/emulated.rs`: the examples build and
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
