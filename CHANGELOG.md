# Changes

Changes to what other repositories depend on (CLAUDE.md, "Interfaces
other repositories depend on"); releases are tagged.

## Not yet released

- New `can_tell` (`include/rnse.h`, `src/hw193.c`; the 193): a frame
  through mailbox 1 of the controller as the software before left it,
  usable before `board_init`, with the watchdog fed while it waits and
  a short wait once nobody acknowledges.  The 193 bench programs
  (`probe193`, `first193`, `sdload193`, `dump193`, `du193`, `keys193`,
  `eject193`, `panel193`, `slot193`, `speed193`, `sdcheck193`) each had
  a copy that held the watchdog for 112 ms a record on a bus with no
  other node, and a unit alone on a bench was reset while they started
  (`docs/custom-firmware.md`, "A 193 alone on its bus").  Not yet tried
  on a unit.
- `build.sh`: an example with a file `bare` links none of the kit's
  code but the files of `src/` that file names, with the example's own
  start-up code (`*.S`) and `link.ld`. Nothing changes for an example
  without one.
- New `examples/side193` (193): a resident beside the stock software,
  which answers `tools/bench193.py` on CAN while the stock software
  runs (`docs/side193.md`). It works in the emulator; one run on a
  bench unit showed two faults that are not fixed.

## v0.4.0

Sound and the radio processor on the 193, and CAN taken as the stock
software left it; on the 192, `can_attach` is `can_init` and the
rest is unchanged.

- `include/rnse.h`, `src/pcm.c`: on the 193, `pcm_start` and the
  streams (`pcm_stream`, `pcm_stream_block`, `pcm_stream_ready`,
  `pcm_stream_stop`, `pcm_stop`) play through the serial sound
  interface at 44.1 kHz.
  - No interrupt: the ring moves on when the program calls
    `pcm_stream_block` or `pcm_stream_ready`, at least once a block.
  - `pcm_put`, `pcm_dma` and `pcm_loop` still return below 0 there.
  - New `pcm_gaps` (both units): how often a block was not ready in time.
- `src/radio.c`: `radio_command`, `radio_source`, `radio_volume`,
  `radio_tune`, `radio_status` and `radio_start` work on the 193.
  - The line is at 0xff915000, with its registers four bytes apart.
  - `radio_start` uses the 193 software's bit rate.
  - `radio_keepalive` sends the 193's as well: 8a every 30 s and
    81 [06] every 60, as its stock software. A bench 193 sent neither
    started again after 13 minutes; sent both it stayed up.
- `include/rnse.h`: `can_attach` takes the controller as the software
  that started the program left it, with no reset and no new bit
  timing, and receives everything as `can_init` does.
  - On the 192 it is `can_init`.
  - On a 193 controller that nobody set up, it falls back to `can_init`.
  - `car_init` uses it (on the bench 193, `can_init` left the bus silent).
- `src/hw193.c`: three faults a bench 193 showed, found with `mon193`.
  - The clock (`millis`, `micros`, `delay_ms`, every time-out) ran at
    twice the world's pace: the tick handler was entered twice for
    each underflow. Programs built with an earlier kit have all their
    times on a 193 halved.
  - The tick is 124620 counts (the timers count 12.462 MHz, not
    12.4876).
  - `can_init` keeps the controller's top control bit set (0x8001,
    0x8000): cleared, the controller moved the identifier in every
    mailbox, which was the silent bus.
- `src/can.c`: `canload` sends its replies up to three times and says
  again which frame is missing (a reply lost on the bus stopped a push).
- `build.sh`: a program is linked without what it never calls of the
  kit (`-ffunction-sections`, `-fdata-sections`, `--gc-sections`).
  `out/<name>-<unit>.bin` is that much smaller (hello for the 192: 10
  KiB for 31), and the 192's larger programs fit its 128 KiB again.
- `src/hw193.c`: `sdhi_write_multi` (CMD25), for a dump to a card.
- `examples/dump193`, `tools/dump193.py`: a window of a 193's memory
  as its stock software left it, over CAN.
- `examples/mon193`, `tools/bench193.py`, `tools/mon193emu.rs`,
  `tools/bench193/`: a monitor for a bench 193 driven over CAN from
  scripts, on a unit and on the emulator alike, and `diff` between the
  two.
- `examples/pcmstream` (both units): tones by stream, reported on CAN 0x7F8.
- `tests/emulated.rs`: `started_as_by_the_sd_loader_193`, and with it
  `sdk_pcm_streams_on_the_193` and
  `car_driver_attaches_to_the_193_s_controller`. Neither needs a disc.

## v0.3.0

Additions, and the 193 as measured on a unit (rnse's
docs/hardware-193.md, "On the bench 193"); nothing that builds for the
192 changes.  The tests are pinned to an emulator that models the bench
193 (rnse b4446e0).

- `include/rnse.h`: `sdhi_start`, `sdhi_read`, `sdhi_read_multi`,
  `sdhi_fast_clock`, `sdhi_trace` (the 193's SD host; stubs on the 192);
  `board_init_step`; `unexpected_hook`; `display_key_followed` and
  `cd_port_set_up_at_start` (per unit: on the 193 `panel_service` does
  not follow the display key, whose motor lines are still assumed, and
  `cd_eject` uses the drive's port as the stock software set it up).
- `include/rnse.h`: `panel_frame_hook` (every faceplate message, before
  the kit reads it; examples/keys193); `board_init_display` and
  `screen_init` (the 193: leave the display unit as the software before
  left it, set it up later).
- The 193 draws through a 16-bit plane (RGB555) with a palette of its
  own; the kit's drawing stays by colour index.
- `build.sh`: an example's `place` file applies to the 192 only; on the
  193 such an example is built for the loader's 1 MiB place like any
  other (it used to be refused).
- `src/hw193.c`: board_init, the faceplate's reset line and the SD host
  as on the bench 193 (read-modify-writes where the stock software does
  them).
- `image_enter` works on the 193 too: every interrupt source masked, the
  timers stopped and the image written back from the cache with the
  watchdog strobed (rnse-linux's `linuxboot` for the 193).
- Examples for the 193: `sdload193` (the SD loader disc: starts
  `RNSE.BIN` from a card, follows CD EJECT), `first193`, `probe193`, `keys193`, `du193`,
  `eject193`, `panel193`, `slot193`, `sdcheck193`, `speed193`.

## v0.2.0

Additions only: nothing that builds today changes.

- `include/rnse.h`: the car (`car_init`, `car_poll`, `car_frame`,
  `car_other`, `car_ignition`, `car_wheel`, `car_wheel_next`,
  `car_wheel_panel`, `car_cluster_text`, `car_cluster_id`, `WHEEL_*`,
  `CAR_KEY_IN`, `CAR_IGNITION`); `can_status`; `panel_put_key`,
  `panel_put_knob`; `panel_service_hook`.
- `src/car.c`, linked by `build.sh`.  A build that names the kit's
  files itself (the ports) needs it only to use the car; `panel_service`
  reaches it through `panel_service_hook`, so leaving it out still links.
- `examples/canbench`, `examples/car`; `tools/canbench.py` (the bus
  through a CANable); `docs/car-bus.md`.

## v0.1.0

The kit as split from rnse.
