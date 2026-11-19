# rnse-sdk

A kit for writing software of one's own for the Audi RNS-E navigation
unit (Aisin AW, Hitachi/Renesas SH-4): start-up code, a hardware layer
for each of the two generations (8P0 035 192 and 193), drivers for the
faceplate, the fold-down display, the CD drive, CAN, the SD cards and
sound, a small C library, and examples.

Its programs are tested on [rnse](https://github.com/jroark/rnse), the
emulator it was split out of (at rnse commit 0e3a2d4ce5941161e17ff4816126281fbcf80088, with its
history).  No firmware is included here; the end-to-end tests need your
own update disc image.

```sh
sudo apt install gcc-sh4-linux-gnu binutils-sh4-linux-gnu   # or: PATH=$PWD/tools/sh4-docker:$PATH
./build.sh 192 hello                       # out/hello-192.bin
rnse custom disc.iso V222 out/hello-192.bin hello.iso    # the emulator's CLI
rnse loader hello.iso --variant V222 --screenshot hello.png
RNSE_DISC=disc.iso cargo test --release -- --nocapture
```

Programs for a real unit must keep the display opening and the disc
ejecting at all times, and nothing here flashes firmware unless you ask
for it: see CLAUDE.md, "Mandatory hardware behavior", and
[docs/custom-firmware.md](docs/custom-firmware.md) for what the
emulator does and does not establish about real hardware.
