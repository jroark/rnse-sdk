# Modern 192 firmware

A native SDK firmware with an Audi-style charcoal/red interface, physical
rotary navigation, FM presets, SD MP3 playback, local contacts, saved destinations, live vehicle
status, an experimental factory telephone client and resident SD applications.
It replaces the operating system; it does not layer over the stock UI.

## Build and preview

Requires Rust, Python 3 and the SDK SH-4 cross compiler. On macOS the existing
Docker wrapper is available:

```sh
export PATH="$PWD/tools/sh4-docker:$PATH"
export RNSE_DISC=/absolute/path/to/stock-update.iso
sdk/modern.sh
```

Use a V222/0371 update disc. SD access uses its card-processor firmware, which
the existing installer preserves. Artifacts are ignored by Git:

- `sdk/out/modern-192.bin` and `.elf`: firmware and debug symbols.
- `sdk/out/app-trip.RNA`: example native app.
- `out/modern/modern-192.iso`: install image with swap recovery loader.
- `out/modern/apps.img`: SD image containing the trip calculator.

The default update index is 0390; set `RNSE_INDEX` to an appropriate newer
index if required by the unit's installed index. Never distribute the disc
image: it includes proprietary material from your source disc.

For a RAM-only preview, without installing into flash:

```sh
target/release/rnse custom "$RNSE_DISC" V222 sdk/out/modern-192.bin out/modern/preview.iso
target/release/rnse loader out/modern/preview.iso --variant V222 --strict \
  --cycles 470000000 --screenshot out/modern/preview.png
```

Interactive use is through the existing emulator faceplate, using the preview
image as the loader (`rnse ui out/modern/preview.iso --variant V222 --loader`).
Check `rnse` usage for current front-end options.

## Controls

RADIO selects FM audio. Turn to choose one of six presets; press to recall.
Bottom left switches between preset selection and manual tuning (50 kHz per
detent). Bottom right saves the frequency to the selected preset.

MEDIA opens the root-directory MP3 list. Bottom left reloads the card; top
right switches SD slots and reloads. Turn to browse and press to play. Bottom
right stops. NEXT/PREV change tracks while SD audio is selected. End of track
advances automatically. Opening another screen keeps playback running; RADIO
stops it and selects the tuner. Loading or scanning a card interrupts SD audio;
app loading restarts the current track afterward, rather than resuming its offset.

INFO opens Applications. Reload and slot selection work as on MEDIA; press to
launch. RETURN exits an app. The volume knob, eject and source keys remain
available. NAME, NAV, TEL and CAR open the built-ins described below.

SETUP offers display brightness and stock recovery. Saved volume, frequency,
six presets and brightness use alternating checksummed records at SRAM offsets
0x3f800 and 0x3fa00, each within its own 512-byte reservation. No user settings
are written to flash. Invalid records restore defaults; audio always starts on
FM, with no automatic SD playback.

Lists retain up to 128 MP3s and 128 apps, inspecting at most 2048 directory
entries. Files use FAT short names. Subdirectories, metadata, seek and route
guidance remain outside this version. SD removal during playback produces an
error; reinsert and reload. The radio driver reports tuning
and source-command failures only where the underlying SDK detects them.

## Built-in applications

**NAME — Address book.** Six local favourites persist across power cycles. Turn
and press to open a slot. Top left edits its name; top right edits its number.
In the editor, turn to choose a character and press to append. Top left removes
a character, top right clears, bottom right saves, RETURN cancels. Numbers use
digits, a leading +, * and #. Bottom left deletes an entry with a separate
confirmation; bottom right copies its number into TEL, where Call is a separate
explicit action. These are local favourites, not a synced phonebook.

**NAV — Destinations.** Four saved places with names, latitude and longitude
persist. Press to open, then use top left for name, top right for latitude and
bottom right for longitude. Decimal degrees accept up to four decimal places;
latitude is bounded to ±90 and longitude to ±180. Bad input remains in the
editor and does not alter saved data. These are stored coordinates: the 192 GPS
baseband driver and map/routing engine remain unimplemented. No position,
distance or guidance is fabricated.

**CAR — Vehicle status.** Shows ignition and key-in-lock state from the documented
0x271/0x2c3 CAN frames, with a 1.5-second freshness limit. Missing or stale data
is displayed as unavailable. It also shows received/dropped CAN frame counters;
bottom right resets the received counter. It sends no vehicle-setting commands.

**TEL — Original Audi module.** Top left explicitly opens the fixed-ID transport
on 0x6c4/0x6c5; top right opens a rotary number editor. Call sends a voice dial
request; when the module reports an incoming call that key becomes Answer.
Bottom left ends/rejects a reported call. The client segments commands, checks
sequence numbers, acknowledges received frames and closes on timeout. It never
automatically retries a dial request. A user action can wait behind one read-only
status query. Call state comes from the module's RING/+CLCC/NO CARRIER replies;
transport connection or a command's OK does not imply an active call.

This telephone client has been tested with protocol fixtures, **not with an Audi
Bluetooth module**. Module discovery/network management, proprietary initialization,
Bluetooth pairing, phonebook synchronization and telephone audio routing still
need real-module verification and implementation. The UI explicitly identifies
the missing call audio. Existing amplifier routing remains unchanged. See
[telephone.md](telephone.md) for reverse-engineered transport evidence. Standard
call commands follow [3GPP TS 27.007](https://www.etsi.org/deliver/etsi_ts/127000_127099/127007/05.07.00_60/ts_127007v050700p.pdf);
this specification does not establish factory-module compatibility.

Contacts and destinations use two independent, checksummed 384-byte SRAM records
at offsets 0x3fc00 and 0x3fd80. They do not overlap stock settings, the shell's
settings reservations or the boot guard at 0x3ff00. A partial write preserves the
previous valid record. A compile-time size check prevents growing into the guard.

## Installation and recovery

Follow [the SDK installation procedure](custom-firmware.md#a-program-in-flash),
using the generated image with its swap loader. Keep a stock update disc ready.
EJECT remains available. In SETUP choose Restore stock firmware, insert the
stock disc, then hold SELECT for two seconds on the recovery screen. RETURN
cancels without changing flash. The existing five-failed-start guard is unchanged;
four quick power cycles before the healthy mark provide the manual escape.
The shell marks a boot healthy after ten seconds with the faceplate online.

This firmware has emulator validation; it has not been tested on a physical
unit. The SDK's existing real-unit installation evidence is documented separately.

## Native application ABI 1

See `sdk/include/rnse_app.h` and `sdk/apps/trip/main.c`. Build another app by
placing its source in `sdk/apps/NAME/main.c` and running `sdk/apps/build.sh NAME`.
Copy `sdk/out/app-NAME.RNA` to the SD root using an 8.3 filename. The firmware
itself does not need rebuilding.

Export these C functions:

```c
void app_init(const RnseAppHost *host);
void app_input(int event, int value);
void app_update(u32 milliseconds);
void app_draw(void);
void app_shutdown(void);
```

The host table remains valid until exit. Drawing is clipped to a 400 x 136
content area. Coordinates start at its upper left; palette indices 0–4 are
background, text, muted text, red and panel. Text uses scales 1–4 and at most
96 characters. Apps cannot change palette through this API.

Input is rotary delta, SELECT or one of four soft keys. Update/draw runs at
most every 50 ms. Keep callbacks short; retain state in BSS. Do not initialize
hardware, replace interrupt vectors or access SD directly. The shell services
audio and the watchdog between callbacks. Only one app runs; shutdown is called
before leaving it. The sample estimates fuel from manually entered distance and
consumption, with no dependency on vehicle data.

Apps link at uncached address `0xac410000`, the alias of physical RAM used at
`0x8c410000`, with a 512 KiB combined image/BSS limit. The shell uses its own
stack. The linker reserves the 64-byte header; the Python packager adds target,
version, sizes, title, callback offsets and FNV-1a checksum. The loader validates
these before calling code and zeros BSS. Apps must use the provided freestanding
build pattern; unresolved runtime/library helpers fail the link.

This is trusted native code, with no process or memory isolation. Checksums
detect corruption, not malicious code. A callback that hangs or corrupts hardware
can require a power cycle; RETURN cannot preempt it.

## Validation

```sh
sdk/tests/check-modern.sh
cargo test --release --lib
PATH="$PWD/tools/sh4-docker:$PATH" RNSE_DISC="$RNSE_DISC" \
  RNSE_BOOT_ROM=/path/to/bootrom-192-unit.bin \
  cargo test --release --test disc modern_ -- --nocapture
```

The resident test requires a cross compiler and verifies strict execution,
real decoded SD audio, native app input/exit, corrupt-app rejection and SRAM
persistence. It writes screenshots into `out/modern/`. The installation test
also needs the dumped boot ROM; without it that test skips. Existing recovery
and guard tests remain applicable.

The `modern_builtins` strict-emulator test drives contact edits, cancellation,
coordinate validation and persistence through the actual SH-4 UI, decodes live
vehicle CAN frames, and exchanges dial/answer/end commands with a fixed-ID test
peer. Screenshots are written beside the app screenshots. Host tests compile the
same phone client and cover sequence handling, early replies, deferred commands,
malformed lines and timeouts. Fixtures do not prove real-module compatibility.
