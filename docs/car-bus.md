# The car's CAN bus

The RNS-E sits on the car's infotainment CAN bus: 100 kbit/s, 11-bit
identifiers, a low-speed fault-tolerant bus (the 192's transceiver is a
TJA1054).  `src/can.c` and the hardware layers send and receive raw
frames; `src/car.c` turns the frames below into what a program wants
(API in `include/rnse.h`, "The car"; `examples/car` shows all of it).

What each frame means is marked as **seen on a unit** (a North American
192 with software 0371 on a bench, a CANable 2 on the bus playing the
car, 2026-11-20) or **emulator only** (the stock software run in rnse,
which reads how the software treats a frame, not how a car sends it).

## Frames the kit uses

| Identifier | Data | Meaning | Status |
|---|---|---|---|
| 0x271, 0x2C3 | byte 0: bit 0 key in the lock, bit 1 ignition on | the ignition lock, every 100 ms; the two identifiers are the same message in two generations of car | the stock software starts on 0x271 `03` (seen); `car_ignition` reads either (0x2C3 emulator only) |
| 0x5C3 | `39 KK` | the steering wheel: KK the key held, 00 none.  The stock software ignores every first byte but 0x39 | seen: see below |
| 0x363, 0x365 | eight characters each | the radio's two lines in the instrument cluster, sent about once a second | seen: the stock software sends `102.1   ` / `\x1c\x1c\x1cFM\x1c\x1c\x1c`, ` SEARCH ` while seeking, ` NO CD `, `NO SDARS`; 0x1C is a filler on either side of a centred text |
| 0x261, 0x263 | the same | the same lines, as the emulated (uncoded) unit sends them | emulator only: which pair a unit uses presumably depends on its coding for the car; `car_cluster_id` picks |

Steering-wheel keys (`KK`):

| Code | The stock software does | Status | The kit makes it |
|---|---|---|---|
| 0x04 | seek up | seen (station changes, ` SEARCH `) | `KEY_NEXT` |
| 0x05 | seek down | seen | `KEY_PREV` |
| 0x06 | volume up | seen (heard) | a detent of `KNOB_VOLUME` up, then five a second while held |
| 0x07 | volume down | seen (heard) | the same, down |
| 0x08 | seek up | emulator only | raw only (`car_wheel_next`) |
| 0x02, 0x03, 0x0B, 0x0C | a command to the radio processor at the release (`cmd = 11 para[0] = 4`), nothing to see or hear | emulator; nothing seen on a unit | raw only |
| 0x40-0xFF | nothing | emulator only | raw only |

Which physical button sends which code is **not yet established**: an
Audi A3/S3 (8P) wheel has a left thumbwheel (roll up/down, press), MODE,
a right thumbwheel (volume, press) and the voice key.  A capture of
0x5C3 in the car, one button at a time, settles it.  The stock software
acts on a seek at the release; the kit puts the key down on the first
frame that carries it and up on the frame with another code, or when
no frame has come for half a second.

## What else the stock software sends and listens to

Seen on the bench unit (stock software, the CANable answering): 0x436
network management, 0x661 status (byte 3 the source: `a0` radio, `a5`
CD, `2f` satellite radio; byte 0 changes with the screen), 0x604, 0x361
once at the start, 0x363/0x365 above.  It never opened its transport
channel to the cluster (TP2.0 on 0x6C0/0x6C1), which the emulated unit
does at once; and the ignition going off changed nothing.  Its network
management, alone, cycles `16 14`, five `16 02 80` with `16 01`, then
`16 04` every 4.2 s; answering it as node 0x16 in the OSEK manner (`36
02 ..`) made it talk faster but did not settle it.  Network management
is not understood yet, and the kit takes no part in it.

The stock software also receives (emulator, V222's mailboxes): 0x351,
0x621, 0x623, 0x625, 0x635, 0x65F, 0x6C1, 0x6C5, 0x6D0/0x6D1 in
mailboxes of their own, and 0x200, 0x2C3, 0x353, 0x359, 0x527, 0x543,
0x5A5, 0x5AB, 0x602, 0x627, 0x651, 0x65D, 0x662, 0x665 through shared
ones.  What they carry is not established here.  Its operating system
also counts 0x5C3, with 0x271 and 0x2C3, among the frames that wake the
unit.

## On the bench

`tools/canbench.py` is the Mac's end of the bus through a CANable 2
(slcan firmware): `run` logs and plays the car (`--ignition`, `--send`,
`--script`, TP2.0 far ends with `--peer`, network management with
`--nm`), `bench` checks the kit's CAN code against `examples/canbench`.
The CANable's 120 ohm termination must be off on this bus.  With a node
that acknowledges, the kit's CAN code answered every frame sent at 2 ms
or more apart, and sixteen frames sent back to back by the unit all got
out; of forty frames sent back to back to it, eight were lost, the
32-frame receive ring filling while the program's own answer waited
for the bus.

## Stock V575 diagnostic disc eject

On the North American 193, adaptation channel 67 (0x43), value 1, is
the one-shot CD/DVD eject documented by Ross-Tech. The V575 software
in 193-575-fw.iso has a K-line diagnostic task and an adaptation reply
routine labeled `adp_kln_ans`. The channel 0x43 code loads 0x43 at
0x806777de and calls the reply routine at 0x806753da via the literal
loaded at 0x80677806. That routine references its K-line error message
at 0x806754ae. The eject code's literal pool at 0x806778b0 points to
the `adp_eject` diagnostic. Addresses are SH-4 addresses in the mapped
flash image.

On the 193 bench with ignition status present, CANable sent TP2.0
channel setup on 0x200 for Navigation (0x37) and Radio (0x56), requesting
tester receive ID 0x300. Neither elicited a reply. The unit continued
sending 0x363, 0x365, 0x436, 0x604 and 0x661. Running the stock V575
image in the emulator likewise delivered those requests to receive
mailbox 31 without a setup reply. This supports a K-line route for the
documented adaptation. A stock eject command over infotainment CAN
remains unverified.

The same channel is a lock as well: while it holds 255 the stock
software refuses CD EJECT (it logs `CDS_Eject2`; rnse's
`docs/hardware-193.md`, "Adaptation, and when the stock software
ejects").  The values are in the battery-backed memory, and
`examples/adp193` reads them from a unit over the bus, with the copies
in the flash store (`tools/adp193.py LOG` prints them by channel).
`examples/adpclear193` is the same program with one write: channel 67
from 255 back to 0, and the block's sum, three words of the
battery-backed memory.  It writes only when the block's marks and sum
are right and the channel is 255, and never to the flash; the copy
there keeps the old value and would come back if the battery-backed
memory were ever lost.
