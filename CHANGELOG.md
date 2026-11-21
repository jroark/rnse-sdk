# Changes

Changes to what other repositories depend on (CLAUDE.md, "Interfaces
other repositories depend on"); releases are tagged.

## Unreleased (branch bench-193)

Additions, and the 193 as measured on a unit; nothing that builds for
the 192 changes.

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
