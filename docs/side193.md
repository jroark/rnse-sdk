# side193: a resident next to the stock software (193)

`examples/side193` is a program the stock software starts from a disc
like any update loader.  Unlike every other program of the kit it does
not take the unit over: it copies a small resident to RAM, puts it in
front of the stock software's tick handler and *returns*, and the
stock software goes on running, radio, screen, keys, eject and
watchdog its own as before.  The resident then answers on CAN.
Nothing is written to flash or battery-backed memory, and a power-off
removes it.

**State: it works in the emulator (test
`side193_runs_beside_the_stock_software`) and it has run on a bench
unit once, where it started and answered, with two faults: the stock
software's interface stopped answering keys some minutes later (the
stock software's own doing, see "The heap under the loader's place"),
and every second frame of an answer of several frames was lost (see
"On a unit").  Neither is fixed.**
This page keeps what has been measured apart from what is assumed.

## Using it

    ./build.sh 193 side193
    rnse custom update.iso V575 out/side193-193.bin side193.iso --index 0990
    python3 -I tools/bench193.py run "emu:--disc side193.iso" tools/bench193/side.script OUT

and, on a bench, the CANable's device in the place of `emu:...`
(`--ignition` with it).  It speaks `examples/mon193`'s protocol, so the
scripts are the same; of their lines it knows `r8/r16/r32`,
`w8/w16/w32`, `block`, `info`, `wait`, `send` and `note`.

| | |
|---|---|
| requests | 0x7F2: seq, op, six bytes |
| answers | 0x7F3: seq, status (0 done, 2 refused, 3 not understood), value, two bytes |
| bytes of a block | 0x7F9: three bytes of offset, five of data |
| records | 0x7F8: `b3` at the start (version, SR), `b2` every two seconds (clock in ms, requests done) |

Ops: 01 READ, 02 ARG, 03 WRITE, 04 BLOCK, 05 INFO as mon193's; 10 OFF
(`"OFF!"`) takes the resident out of the stock software's table for
good.  INFO items: 0 version, 1 clock, 2 SR, 3 VBR, 5 requests lost,
6 what answers (2: this), 9 frames given up, 10 the stock tick
handler, 11 the longest tick's work in timer counts.

What it refuses: a write to the flash or the battery-backed memory;
an address that is not a multiple of the size, or in the reserved top
of the 29-bit space.  **It has no exception handler of its own**: an
on-chip register address the chip does not have can end in the stock
software's exception handling and a restart.

## On a unit

Bench unit B (193, shows SW 0310), 2026-11-22, the disc
`side193v1-v575-0990.iso` (program version 1), a CANable on the bus,
`tools/bench193/side.script`.  Logs: `work/side193/` (not in the
repository).

* The stock software called the disc's program about 102 s after its
  first CAN frame.  The resident's start record came, every request of
  the script was answered, and the stock software's frames (0x436,
  0x661, 0x363, 0x365, 0x604) went on through it and for the minutes
  after.
* The unit's OS is **V57510A0** (module size 0x2a8d80, checksum
  0xcf8eddc8), not the emulator's V57510AA.  VBR (0x80400290), the
  tick handler (0x804cc3e4) and the handler table (0x8e8d0400) are the
  same in both.  PVR 0x10300700, CVR 0x71440211, PRR 0x412, CCR 0x101.
* The resident's clock counted 63.2 s in 63 s.  Its longest tick's
  work was 0x8d timer counts of the 12462 in a millisecond.  No request
  lost, no frame given up.
* The mailbox interrupt mask read 0x02040180 (the emulator: 0x03040180).
* **Fault: of an answer of several frames, every second frame did not
  arrive** (the block of 256 bytes came with offsets 0, 0x0a, 0x14,
  ..., 2 ms apart, and its CRC was wrong); answers of one frame all
  came.  Not reproduced in the emulator.  Likely cause, not confirmed:
  the low half of the transmit-pending register (0xffdf0022, read as 16
  bits) reads 0 on the chip while the mailbox is still sending, so the
  next frame is written over a mailbox at work.  The kit's own driver
  (`src/hw193.c`) waits for the acknowledgement flag (0xffdf0032)
  instead, and that works on this unit.
* **The interface stopped answering the knob and the keys** some
  minutes after the start (the user's report; whether NAV had been
  pressed is not known).  The resident was then switched off (OFF:
  answered, the stock handler back in the table) and the interface
  stayed dead, while the stock software's CAN frames went on.  A power
  cycle is the way out; the disc comes out in the time before the stock
  software calls it.

## How it is hooked in

* The stock application calls the program's first entry and then its
  third.  The third copies the resident (about 2 KiB of code) to
  0x8bff0000, writes the copy back from the data cache and drops the
  lines from the instruction cache, clears its variables and calls
  `side_init`.
* `side_init` finds the stock software's table of interrupt handlers
  from the code of its interrupt entry (at VBR + 0x600: the literal it
  loads before `mov.l @(r0, r1), r3`; 0x8e8d0400 in V57510AA), checks
  that the tick's entry (code 0x400) is an address in the OS module,
  and puts `side_entry` there, interrupts held off for those
  instructions.
* `side_entry` runs at the tick's interrupt level on a stack of its
  own, with every register it may change put away, calls `side_tick`
  and jumps to the stock handler with the registers as they came.
* A second start while one is at work (the disc put in again) changes
  nothing.  A canary and the resident's mark are checked every tick;
  if either is wrong the stock handler goes back in the table.
* CAN: mailboxes 7 (sends) and 8 (receives 0x7F0-0x7F3), which the
  stock software leaves out of use, their interrupts masked.  Only
  their own bits of the shared flag registers are written.  One frame
  a tick at most; a frame nobody acknowledges within 50 ms is taken
  back.

## What the stock software does when the "loader" returns

Emulator only (V575, OS V57510AA, of the 0320/0350 disc; 2026-11-22).

* The application (`APM`) calls the first entry (return address
  0x01c0a25a) and then the third (0x01c0a268), both with SR =
  0x40000000 (interrupts open), VBR = 0x80400290, on the application
  task's stack (0x0ca98908).  r4-r6 = 1, 1, 1 at the first.
* When the third entry returns, the routine that called it returns 0
  to its own caller like any other: there is no "must not come back"
  path.  With a program that only returns, the stock software ran on
  for 120 s without a fault, a restart or a second call, its cyclic
  CAN frames (0x261, 0x263, 0x604, 0x661, 0x6c0) at their usual rates.
* With the disc put in 20 s after the start (the interface up), the
  interface went on answering keys after the return: RADIO, SETUP and
  RETURN drew the same screens as in a run without the update.
* **NAV then shows the page "Software update CD/DVD 0330" and the
  interface stops answering keys.**  With the disc in the drive from
  the start, the emulated hand-over comes 2 s in, before the interface
  is up, and that page is what the unit shows from then on.  Taking
  the disc out in that state changes the text to "Empty" and does not
  free the keys.  The next section has why.
* Taken out and put in again, the disc's program is called a second
  time.

## The heap under the loader's place

Emulator (V57510AA), 2026-11-22.  This is why the interface dies, and
it happens with a program that does nothing but return, with no
resident at all.

* The application copies the loader's image to the address in the
  image's own header (word 0x70, masked to 29 bits; 0x09b60000 for
  every loader) for the length in word 0x74, and calls the entries it
  finds 0x80 and 0x88 into the copy (`APM` 0x01c0a190-0x01c0a276; the
  header words are read by 0x01c0a0e8 and 0x01c0a0f2).
* That memory is not set aside for it.  The stock software has a heap
  of pictures there: before any update the range from 0x09cf04d0 up is
  in use 19 s after the start and all of 0x09b60000-0x09e83500 later,
  with block headers between the pictures (one at 0x09e836f0) and
  pixels of 0x7c1f in the blocks.  The copy goes over whatever blocks
  lie in its range, which a stock loader, never returning, need not
  care about.
* After the return the first thing that wants that heap fails: with
  NAV the log has `malloc error MEMADR:a9e836fc, TSKID:54, CALLADR:f5`
  ten times and more, and from then on no key changes the screen.  A
  run of the same keys without the update has none of those lines.
* Whether the bench unit's dead interface was this has not been
  checked on the unit (its build differs), but it fits: dead with the
  resident switched off, the stock software otherwise running.

So a program that returns must not let the application copy it over
that heap.

### The fix to build

This needs a relocated, short module image.  Moving only the stack
does not fix it: the application has already copied the module over
the heap before either of the program's entries runs.  The current
called piece already uses the application's stack for only four saved
registers, and the resident switches to its own stack on every tick.

The unused RAM measured above leaves room for the two parts without
using the heap:

* Link the short called piece at 0x0bfe0000, in the 64 KiB immediately
  below the resident.  Keep the resident at 0x8bff0000 (physical
  0x0bff0000), including its private stack.
* Make the copy of LDM on the disc say 0x0bfe0000 in its header word
  0x70.  Put only the header and the actual side193 image in header
  word 0x74's length and recompute its checksum over that length.  The
  directory entry can retain the stock module's padded allocation.
  `rnse custom` currently preserves the stock LDM load address and
  length, so it needs this relocation support before such a disc can
  be made.
* Let the third entry copy the resident upward to 0x8bff0000 as it
  does now, clear its variables, install the tick hook and return.
  The called piece can continue using the application task's stack
  for its short call because that stack is outside both side193
  regions and is restored before the return.

Before putting this on a unit, an emulator test must show that the old
LDM range at 0x09b60000 is not replaced by the module, its heap block
headers remain the same as a stock control run, NAV and RADIO still
answer after the return, and taking the disc out and putting it back
does not make a second resident or change the stock tick handler saved
by the first one.  The first hardware trial should then repeat
`tools/bench193/side.script`, exercise NAV and RADIO, and keep physical
display opening and disc ejection available.

## The stock software's interrupts and CAN mailboxes (emulator)

* Handlers are looked up in the table at 0x8e8d0400 by interrupt code
  / 0x20 and entered by RTE with SR = 0x40000000 | the level byte from
  0x8e8d0600, the interrupted program's registers in place and PR at
  the entry's way out (0x804008e0).  Seen running: codes 0x3e0, 0x400
  (the tick, handler 0x804cc3e4, level 8), 0x480 (CAN timer), 0x5e0,
  0x780, 0x880 (CAN), 0xd20, 0xdc0, 0xf00.
* Mailboxes: 0 receives everything else; 2 and 9-17 send (0x6c0,
  0x661, 0x261, 0x263, 0x604); 18-31 receive, as the boot ROM set them
  up.  1 and 3-8 are out of use.

## RAM (emulator)

* The loader's place (0x09b60000) is **not** free after the return:
  the OS (code at 0x804d31e6-0x804d3c66) stores to 0xa9bc1000, 0x61000
  into it, when the interface is used.
* Never written in a 120 s session with keys (compared with the
  scrambled RAM the run started from): 0x08396000-0x09643000,
  0x0aa09000-0x0ba00000, 0x0ba01000-0x0bcd6000, 0x0bcd8000-0x0c000000.
  A session with a navigation DVD, a card or sound may use more.  The
  resident is at the top of the last one (0x0bff0000, 64 KiB).

## Not done

* The two faults of the unit run: the heap under the loader's place,
  and the frames lost from answers of several frames.
* A way to bring a newer resident or a small program in over CAN:
  version 1 has none, so every change to it is a new disc.
* The stock software's debug text (its `printf` is in the flash and
  can only be caught with the processor's break controller, which the
  emulator does not model yet), values watched for changes, programs
  brought in over CAN, a log call for them (`side_log` is a stub).
* A log on an SD card.
* The 192.
