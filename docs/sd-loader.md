# The SD loader

## One disc for any unit

    PATH="$PWD/tools/sh4-docker:$PATH" sh tools/build-universal-loader.sh /path/to/stock.iso

makes `out/universal-loader/loader.iso` from a stock update disc: a copy
with the SD loader in the update loader's place for **every** kind of
unit the disc has software for, and the software index 0990 for each.
Burn it as a disc image.

- **Which units.**  A North American update disc has software for the
  192 (V222) and the 193 (V575); a European one for the 192 (V220) and
  the 193s (V574, VB48).  So two discs cover every unit, one from each.
  On the 192s the loader is `examples/sdboot` (below), on the 193s
  `examples/sdload193`.
- **Whatever software the unit has.**  The installed software starts a
  disc's loader when the disc's index is higher than its own in the
  second and third digit, the first being the same; it also checks the
  project code and that every module on the disc adds up, and nothing
  about versions.  0990 is higher than any unit's whose index begins
  with 0.  (One that began with X or Y would want that letter; none is
  known, and `rnse` writes digits only.)
- **Nothing is installed.**  The loader ends in a restart, so the unit
  keeps its software and its index and takes the same disc again every
  time.  A unit that is given a stock update afterwards has a higher
  index than before and still a lower one than 0990.
- **Cards on a 192.**  The card processors need a program, which the
  card driver takes from the unit's flash where that holds the one it
  knows (`src/sd.c`): the North American software 0367 and 0371 has it,
  and the European 0815 of the last two update discs.  On any other
  software the driver reads the same program from the operating system
  on the loader's own disc, so the script wants a disc that has it: the
  update discs with the indexes 0320/0350 or 0310/0340 (North America),
  0310/0740 or 0300/0730 (Europe), and says so otherwise.  The loader's
  disc then has to be in the drive when a card is started.
- **Alone on a bench.**  The loaders wait for no acknowledgement on the
  CAN bus (`docs/custom-firmware.md`, "A 193 alone on its bus").

Checked in the emulator (`tests/emulated.rs`,
`universal_loader_starts_on_every_version`, with `RNSE_DISCS` naming a
directory of update disc images): seven North American and eight
European images, each installed in turn for each of its variants, 38
units in all, the loader disc in the drive and nothing else on the bus.
On every one the installed software starts the loader and the loader a
program from a card.

On units: the 193's loader with index 0280 on a North American 193 with
software 1054, alone on its bus.  The disc as this script makes it, the
192 on other software than 0371, and every European unit: not yet.

## The 192's loader

This loader boots once from an update disc, waits for a card in either slot,
loads `RNSE.BIN` from its FAT root directory, and returns directly to its
waiting screen after the test. The disc stays in the drive. On other installed software than the card driver knows it takes the card
processors' program from the disc (above).
The stock firmware is involved only in the initial disc launch.

Build from the isolated worktree:

```sh
PATH="$PWD/tools/sh4-docker:$PATH" sh tools/build-sd-loader.sh /path/to/stock.iso 0380
```

Use a disc index higher than the installed index. Output is
`out/sd-loader/sdboot.iso`, sample `RNSE.BIN`, `disc-info.txt`, and
`SHA256SUMS`. Burn the ISO as a disc image. Only the V222 loader is replaced;
this is not a 193 loader. The sample completes with status 17.

Build tests with this worktree's SDK and copy the resulting binary to the
card as `RNSE.BIN`:

```sh
PATH="$PWD/tools/sh4-docker:$PATH" ./build.sh 192 returntest
cp out/returntest-192.bin /Volumes/YOUR_CARD/RNSE.BIN
```

Return normally from `main()` or call `loader_return(status)` explicitly.
Outside a valid SD loader session, that function returns and normal main
completion retains the SDK's idle loop. `restart()` still resets the unit;
replace test calls to it when direct return is wanted. `stock_update()`
still performs an update and exits the session.

## Several programs on one card

Every file `NAME.BIN` in the card's top directory that could be a
program is one (up to nine): names starting with `_`, the Mac's `._`
files, are left out, and so is any `.BIN` larger than a program can be
(0x1c000 bytes), such as the Linux kernel `VMLINUX.BIN` that
`linuxboot` reads ([linux.md](https://github.com/jroark/rnse-linux/blob/main/docs/linux.md)).
With one on the card, it runs when the card goes in, as before.  With
several, the loader shows a menu: the select knob, NEXT and PREV
choose, SELECT runs.  When the program returns, the menu comes back
with it chosen, and RETURN runs it again.  Taking the card out and
putting it back starts over.  The name of the program run last is
kept with the session's control data (`loader_note`, `loader_noted`
in `src/wayback.c`), since the loader is started afresh after
every return.

**No new disc is needed for this.**  A loader on the card as `RNSE.BIN`
is left out of the menu (it is recognised by the banner every version
prints, `rnse sdk: sdboot`).  A disc with the earlier loader, which
runs `RNSE.BIN` and nothing else, starts this one from the card; this
one's first `loader_run` saves *itself* as the loader programs return
to, so from then on until the unit is switched off it is the resident
loader, menu and all.  So: the new `sdboot-192.bin` on the card as
`RNSE.BIN`, the tests under other names:

```sh
PATH="$PWD/tools/sh4-docker:$PATH" ./build.sh 192 sdboot
cp out/sdboot-192.bin /Volumes/YOUR_CARD/RNSE.BIN
PATH="$PWD/tools/sh4-docker:$PATH" ./build.sh 192 returntest
PATH="$PWD/tools/sh4-docker:$PATH" ./build.sh 192 returntest2
cp out/returntest-192.bin /Volumes/YOUR_CARD/TEST17.BIN
cp out/returntest2-192.bin /Volumes/YOUR_CARD/SECOND.BIN
```

`tests/emulated.rs`, `sd_loader_offers_a_menu_of_programs`, checks the menu,
the return to it, RETURN, and a loader chosen from the menu taking over.

At the loader, a card already present runs once. Otherwise insert a card;
slot one wins if both are detected together. After completion or an error,
remove and reinsert that card to retry, or press RETURN to rerun it. Holding
a key counts as one press. The key next to the power button opens and
closes the display (in front of the card slots and the drive) and CD EJECT
ejects the disc, wherever the loader waits, also while it starts a card.
There is no key or sequence that reaches the stock updater or writes the
flash, and no automatic restart or update.

Both insertion and removal are debounced for 100 ms. Invalid/missing files
and read failures remain on the loader screen. Removal while reading must
fail the load rather than execute a partial image. Remove the card before
changing files on a computer, then reinsert it.

`ports/dvd/dvd` is one such program, a DVD player: `DVD.BIN` on the
card, and the loader's disc taken out for a DVD once it runs
([dvd.md](https://github.com/jroark/rnse-ports/blob/main/docs/dvd.md)).

## RAM and return contract

Payloads use the existing SDK 192 layout at `0x8df00000`, a declared 128 KiB
runtime region, and a binary no larger than `0x1c000` bytes. Entry points must
be aligned and lie inside the loaded image. The loader preserves its runtime
region before replacing it, then restores it with an uncached trampoline,
cache maintenance, interrupts masked, timers and DMA stopped, and watchdog
service during copying. The SDK startup runs again and consumes the return
status without going through the ROM reset entry.

A program goes back to the loader only between two exchanges with a
card processor: RETURN held during a read takes effect at the read's
end (`sd_busy` in `src/sd.c`, `follow_return` in `src/cd.c`).
The loader then takes the card as the program left it (`sd_attach`);
a card that has gone in since is started the stock driver's way, on
the processor's program without a reset (`sd_restart`), and only when
that fails is the processor started anew (`sd_init`).  When all three
fail the screen says what `sd_attach` and `sd_restart` answered above
the load error; CAN records 0x7f8 carry them as items 4 and 6 (+ slot).

Reserve `0x8c7c0000..0x8c800000` (including its uncached aliases) in all tests:
control data starts at `0x8c7c0000`, the restore trampoline and parameters at
`+0x1000` and `+0x1800`, the recovery stack ends at `0x8c7e0000`, and the
saved loader occupies `0x8c7e0000..0x8c800000`. The backup has a checksum;
a damaged backup is never executed. Tests must return cooperatively and
respect this memory. Arbitrary hangs or memory corruption require a reset.

## Verification

```sh
PATH="$PWD/tools/sh4-docker:$PATH" RNSE_DISC=/path/to/stock.iso \
  cargo test --release -p rnse-sdk --test emulated sd_loader_returns_and_rearms_on_reinsertion -- --nocapture
cargo test --release --lib
```

The integration test uses the real stock software for the initial launch,
then exercises repeated sessions in both slots with cache checking and flash
comparison. The `returntest` fixture returns from main; `returnapi` explicitly
returns status 23. A second visible test, `returntest2`, displays
"SECOND SD TEST" for five seconds before returning status 23:

```sh
PATH="$PWD/tools/sh4-docker:$PATH" ./build.sh 192 returntest2
cp out/returntest2-192.bin /Volumes/YOUR_CARD/RNSE.BIN
```

The `second_sd_test_returns_status_23` integration test verifies that payload
with flash comparison and cache checking.

On hardware, leave the disc in place for at least three cycles, alternate
test images on the card, and verify the loader returns without the stock
startup screen. Verify RETURN reruns once, missing/invalid files leave the
loader operable, and the display remains lit during SD initialization.
Emulator results do not establish hardware acceptance.

Hardware verification: the burned loader disc and a 32 MB FAT16 card were
used to run the first test and return to the waiting screen with status 17.
The card was then moved to a Mac, replaced with `returntest2`, and reinserted;
the second test also worked, with the loader disc remaining in place.
