# Persistent SD test loader (North American 192)

This loader boots once from an update disc, waits for a card in either slot,
loads `RNSE.BIN` from its FAT root directory, and returns directly to its
waiting screen after the test. The disc stays in the drive. Installed flash
must contain the V222 0371 DSP program understood by the SDK SD driver.
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
PATH="$PWD/tools/sh4-docker:$PATH" sdk/build.sh 192 returntest
cp sdk/out/returntest-192.bin /Volumes/YOUR_CARD/RNSE.BIN
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
`linuxboot` reads ([linux.md](linux.md)).
With one on the card, it runs when the card goes in, as before.  With
several, the loader shows a menu: the select knob, NEXT and PREV
choose, SELECT runs.  When the program returns, the menu comes back
with it chosen, and RETURN runs it again.  Taking the card out and
putting it back starts over.  The name of the program run last is
kept with the session's control data (`loader_note`, `loader_noted`
in `sdk/src/wayback.c`), since the loader is started afresh after
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
PATH="$PWD/tools/sh4-docker:$PATH" sdk/build.sh 192 sdboot
cp sdk/out/sdboot-192.bin /Volumes/YOUR_CARD/RNSE.BIN
PATH="$PWD/tools/sh4-docker:$PATH" sdk/build.sh 192 returntest
PATH="$PWD/tools/sh4-docker:$PATH" sdk/build.sh 192 returntest2
cp sdk/out/returntest-192.bin /Volumes/YOUR_CARD/TEST17.BIN
cp sdk/out/returntest2-192.bin /Volumes/YOUR_CARD/SECOND.BIN
```

`tests/disc.rs`, `sd_loader_offers_a_menu_of_programs`, checks the menu,
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

`sdk/examples/dvd` is one such program, a DVD player: `DVD.BIN` on the
card, and the loader's disc taken out for a DVD once it runs
([dvd.md](dvd.md)).

## RAM and return contract

Payloads use the existing SDK 192 layout at `0x8df00000`, a declared 128 KiB
runtime region, and a binary no larger than `0x1c000` bytes. Entry points must
be aligned and lie inside the loaded image. The loader preserves its runtime
region before replacing it, then restores it with an uncached trampoline,
cache maintenance, interrupts masked, timers and DMA stopped, and watchdog
service during copying. The SDK startup runs again and consumes the return
status without going through the ROM reset entry.

Reserve `0x8c7c0000..0x8c800000` (including its uncached aliases) in all tests:
control data starts at `0x8c7c0000`, the restore trampoline and parameters at
`+0x1000` and `+0x1800`, the recovery stack ends at `0x8c7e0000`, and the
saved loader occupies `0x8c7e0000..0x8c800000`. The backup has a checksum;
a damaged backup is never executed. Tests must return cooperatively and
respect this memory. Arbitrary hangs or memory corruption require a reset.

## Verification

```sh
PATH="$PWD/tools/sh4-docker:$PATH" RNSE_DISC=/path/to/stock.iso \
  cargo test --release --test disc sd_loader_returns_and_rearms_on_reinsertion -- --nocapture
cargo test --release --lib
```

The integration test uses the real stock software for the initial launch,
then exercises repeated sessions in both slots with cache checking and flash
comparison. The `returntest` fixture returns from main; `returnapi` explicitly
returns status 23. A second visible test, `returntest2`, displays
"SECOND SD TEST" for five seconds before returning status 23:

```sh
PATH="$PWD/tools/sh4-docker:$PATH" sdk/build.sh 192 returntest2
cp sdk/out/returntest2-192.bin /Volumes/YOUR_CARD/RNSE.BIN
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
