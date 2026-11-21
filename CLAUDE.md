# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A kit for writing software of one's own for the Audi RNS-E navigation unit (Aisin AW, SH-4 CPU): C, built with an SH-4 GCC by `./build.sh 192|193 [example]`. It was split out of the emulator's repository, `rnse` (https://github.com/jroark/rnse), with its history; the emulator is what its tests run the programs on. Both models are covered: the 192 (SH7750R, variants `V220`/`V222`) and the 193 (a Renesas R8A77730, SH-Navi family, `V574`/`V575`/`VB48`).

Firmware is copyrighted and must never be committed. `.gitignore` blocks `*.iso`, `*.bin`, `*.KWI`, `*.MOT`, `/firmware/`, `/out/`; do not add firmware-derived files (flash dumps, extracted modules, SD card images) to the repository under other names either.
Never commit the products of `build.sh` (`out/`), card images, disc images or boot ROM dumps.

## Mandatory hardware behavior

- **Always preserve physical ejection.** Every custom firmware, loader,
  diagnostic and test payload must support opening/folding down the display
  and ejecting the disc. Keep faceplate polling and eject handling responsive
  on normal screens, error screens and during long-running operations. Do not
  repurpose the display/open key without another accessible physical way to
  open the display and reach the disc-eject control. SDK `KEY_DISPLAY` is
  emulator `EJECT`; `KEY_CD_EJECT` is a separate key. Verify the actual eject
  command path before providing hardware test builds, including across handoffs.
- **Hold RETURN for two seconds to return to the loader.** Every payload
  launched by the SD loader must implement this shortcut from every screen,
  close active apps and stop playback before calling `loader_return(status)`.
  A short RETURN retains its normal back/cancel behavior; a long hold must not
  also trigger that short action or immediately relaunch the payload after
  returning. Return directly to the resident loader without resetting or
  invoking an updater. Outside a loader session, report that return is
  unavailable and remain operable, with eject still working.
- **No firmware-flash option unless the user explicitly asks for it.** Default
  custom loaders, test payloads and UIs must neither flash firmware nor expose
  any updater/install/restore/erase option or hidden button sequence that can
  do so. This includes double-SETUP, `stock_update()`, firmware removal and
  automatic updater handoffs. Permission to build, test, recover, return to
  the loader or eject a disc is not permission to flash. Existing updater
  mechanisms described elsewhere in this file are technical background, not
  authorization to expose or invoke them. Include a flash option only when
  explicitly requested by the user.

Hardware incident reported 2026-11-15: double-SETUP on the original loader
invoked an updater and flashed firmware whose eject button did not work. The
disc was trapped and the user had to disassemble the RNS-E. These requirements
apply to all future hardware builds.
## Commands

```sh
./build.sh 192 hello                  # out/hello-192.bin (and .elf, with debug information)
./build.sh 192 ../elsewhere/myprogram # an example kept outside examples/ (other repositories do this)
sh tools/build-all.sh                 # every example for every unit it supports (tools/examples.txt)
sh tests/check-modern.sh              # host tests of the modern firmware's phone code
cargo test --release                  # the programs on the emulator: skip without RNSE_DISC (see below)
RNSE_DISC=disc.iso cargo test --release -- --nocapture
PATH=$PWD/tools/sh4-docker:$PATH RNSE_DISC=disc.iso RNSE_BOOT_ROM=rom.bin cargo test --release   # everything for the 192
```

Without an SH-4 GCC on the PATH install `gcc-sh4-linux-gnu binutils-sh4-linux-gnu` (Debian/Ubuntu) or put `tools/sh4-docker` on the PATH (runs one in a container; needs Docker). The tests in `tests/emulated.rs` return early, and report success, without `RNSE_DISC` (an update disc image, firmware: never committed) or without a cross compiler, and those that compare with a real 192 boot ROM do the same without `RNSE_BOOT_ROM` (the file `rnse report --rom` writes). A green run without them proves nothing. A local disc image may be sitting in the repository root as an ignored `*.iso`; use it if present. They run whole boots and updates and take minutes.

Scripts that pack discs or cards (`modern.sh`, `tools/build-sd-loader.sh`) need the emulator's command line, `rnse`: `RNSE=/path/to/rnse`, or one on the PATH (`cargo install --git https://github.com/jroark/rnse`); `tools/rnse.sh` finds it.

## Layout

- `src/` start-up code (`crt0.S`), the hardware layer per model (`hw192.c`, `hw193.c`, which replay the stock software's bring-up), drivers: faceplate (`panel.c`), fold-down display (`display.c`: the motor that uncovers the drive and the card slots, stopped by the tick when the position stops changing), CD drive (`cd.c`), CAN (`can.c`), SD cards through the 192's card processors (`sd.c`), sound (`pcm.c`), the radio processor (`radio.c`), flash and its guard (`flash.c`), the way back to the stock software (`wayback.c`, `enter.c`).
- `include/rnse.h` the API; `include/rnse_app.h` the resident apps' API (`apps/`).
- `libc/` a freestanding C library for larger programs; `third_party/libc` the few headers vendored code wants.
- `examples/` programs. Those meant for a real unit: `report` (registers, the boot ROM, on a 192 a program of its own in the card processors to read their straps and time their clock; sent out on screen and CAN, reassembled by `rnse report`), `drive` (a timed log of the CD drive across an eject), `suite` (bench measurements as CAN records). `sdboot` is a loader for the disc that starts `RNSE.BIN` from an SD card (`docs/sd-loader.md`; `returntest`, `returntest2`, `returnapi` are its test payloads), `base` a firmware to install in flash (radio, MP3 player, the way out), `player` an MP3 player, `modern` a whole firmware for the 192 (`docs/modern-192.md`, `modern.sh`), `swap` what a unit without software runs from a firmware disc.
- `tools/sh4-docker` the cross compiler in a container; `tools/build-sd-loader.sh`; `tools/rnse.sh`; `tools/examples.txt`.
- `tests/emulated.rs` the programs run on the emulator (crate `rnse`, pinned in `Cargo.toml` and `Cargo.lock`).
- `docs/custom-firmware.md` what the kit does and does not establish about real hardware: keep it honest when adding to the SDK.

A `docs/....md` or `src/....rs` named in a comment that is not in this repository is in `rnse` (the emulator's reverse-engineering notes: `docs/hardware.md`, `docs/hardware-193.md`, `docs/boot-rom.md`, `docs/firmware-format.md`).

## Interfaces other repositories depend on

`rnse-ports` (Doom, Quake, umac, Windows 3.11, NES, DVD), `rnse-linux` and `rnse-sdbridge` build on this kit from a checkout named by `RNSE_SDK` and pin it by tag. Treat these as an API and change them only with a tag and a note in the changelog: `build.sh`'s arguments (unit, example name or directory) and its `out/<name>-<unit>.bin` output; `link.ld` and its `LOAD`/`SIZE` symbols; the list of `src/` files a program links (the ports' build scripts name them); `include/rnse.h`; `libc/`; an example directory's `flags` and `libs` files.

## Things that are easy to miss

- **The way back.** `rnse custom` packs the stock loader into the image after the program; without it a unit would start the program at every start with no way to get the disc out. Read "The way back" in `docs/custom-firmware.md` before changing start-up code or the image layout.
- **A program in flash** (`rnse firmware`, 192) is taken out again only by the guard in `src/flash.c`; nothing else would. Do not weaken that path.
- **`--strict` realism**: the tests run with arbitrary RAM, the watchdog armed, timers and interrupts in use and the CPU's caches modelled. Code that works only on zeroed RAM or without cache maintenance will fail them, as it would on a unit.
- An example's file `flags` gives single files their own compiler options (`-m4` for FPU code: "What a program gets" in `docs/custom-firmware.md`); its file `libs` names directories of `third_party/` to build with it.
- Time in the emulator is counted in CPU cycles; tests convert with `Machine::cycles_per_ms()`. The European 192's own start of a loader can collide with one of its timer handlers; `started_by_stock_software_with` retries at a neighbouring CPU ratio for exactly that fault.
