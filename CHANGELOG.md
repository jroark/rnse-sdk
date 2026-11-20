# Changes

Changes to what other repositories depend on (CLAUDE.md, "Interfaces
other repositories depend on"); releases are tagged.

## Unreleased

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
