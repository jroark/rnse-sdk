#!/usr/bin/env python3
"""What examples/adp193 (or adpclear193) sent, from a CAN log.

    adp193.py LOG [LOG ...]

LOG: a log of tools/canbench.py ("rx 7f8 [8] ..") or the emulator's
--trace-can output.  Prints the stock 193 software's adaptation block as
the program read it from the battery-backed memory (every channel by its
number), the copies in the two banks of the flash store, and what the
program did.  The last whole run in the log is the one shown.  No
packages needed.
"""
import re
import sys

LINE = re.compile(r'\b(?:rx|tx)\s+([0-9a-fA-F]{3})\s+\[(\d)\]((?:\s+[0-9a-fA-F]{2})*)')

# The stock software's channel table (V575, at 0x804b8270 in 10AA): the
# channel each of the 39 values belongs to.
CHANNELS = [64, 65, 1, 2, 66, 67, 69, 68, 70, 99, 98, 63, 123, 117, 112, 3, 7, 114, 115,
            118, 119, 97, 125, 129, 130, 135, 136, 137, 138, 139, 8, 16, 17, 5, 116, 111,
            10, 4, 18]
# What the stock software calls each (its routines' own names; rnse's
# docs/hardware-193.md, "The channels").
NAMES = {1: "tirelen: tyre circumference, mm", 2: "tirelen: pulses per turn of the wheel",
         3: "config", 4: "RvcActivation: rear view camera", 5: "vthreshold1: speed threshold",
         7: "config2", 8: "(sound setting, 0-5; else read as 3)", 10: "", 16: "NoNavi",
         17: "enabfunc2", 18: "StartUpScreen", 63: "secaccess", 64: "swchksum (test)",
         65: "diskread (test)", 66: "self (test)", 67: "eject", 68: "tstimage (test)",
         69: "dispmec (test)", 70: "displight", 97: "swupgrade", 98: "trgtowtch (test)",
         99: "transmode: transport mode", 111: "(test)", 112: "sourcesw (test)",
         114: "(test picture)", 115: "(test picture)", 116: "(test)", 117: "soundpos (test)",
         118: "(test picture)", 119: "(test picture)", 123: "volume", 125: "settings (test)",
         129: "cstkey", 130: "cstpwoff", 135: "cstent", 136: "cstdrvpos", 137: "cstansw",
         138: "cstautored1", 139: "cstautoredrpt"}
END = {0: "read only: nothing written",
       1: "channel 67 cleared and read back",
       2: "NOT written: the block was not as expected (marks, sum, or channel 67 not 255)",
       3: "WRITTEN BUT READ BACK OTHERWISE"}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    runs = []
    for p in sys.argv[1:]:
        with open(p, errors='replace') as f:
            for line in f:
                m = LINE.search(line)
                if not m or int(m.group(1), 16) != 0x7f8:
                    continue
                d = bytes(int(b, 16) for b in m.group(3).split())
                if len(d) != 8 or not 0xa0 <= d[0] <= 0xa5:
                    continue
                if d[0] == 0xa0:
                    runs.append({})
                if runs:
                    runs[-1][(d[0], d[1])] = (int.from_bytes(d[2:6], 'big'), int.from_bytes(d[6:8], 'big'))
    whole = [r for r in runs if any(k[0] == 0xa5 for k in r)]
    if not whole:
        print("no whole run of adp193 in the log (%d begun)" % len(runs))
        return 1
    r = whole[-1]
    mark, version = r[(0xa0, 0)]
    stored, tail = r[(0xa1, 0)]
    total = r[(0xa1, 1)][0]
    print("run %d of %d" % (len(whole), len(whole)))
    print("block: mark %08x (%s), end mark %s, version ..%04x" % (
        mark, "ok" if mark == 0x12345678 else "WRONG", "ok" if tail else "WRONG", version))
    print("sum: stored %08x, of the values %08x: %s" % (stored, total, "ok" if stored == total else "MISMATCH"))
    for i, ch in enumerate(CHANNELS):
        v = r.get((0xa2, i))
        text = "missing" if v is None else "%5d  0x%04x" % (v[0], v[0])
        print("  channel %3d  %s  %s%s" % (ch, text, NAMES.get(ch, ""),
              "   <- 255: stock refuses CD EJECT" if ch == 67 and v and v[0] == 0xff else ""))
    for b in (0, 1):
        v = r.get((0xa3, b))
        if v is None:
            print("flash store bank %d: no record" % b)
            continue
        seq, flags = v
        print("flash store bank %d: sequence %08x, mark %s, checksum %s, block 3 %s" % (
            b, seq, "ok" if flags & 1 else "absent", "ok" if flags & 2 else "wrong",
            "there" if flags & 4 else "not there"))
        c = r.get((0xa4, b))
        if c:
            print("    its block 3: sum %08x, channel 67 = %d" % c)
    for (test, status), (value, low) in r.items():
        if test == 0xa5:
            print("end: %s; channel 67 now %d, sum %s (..%04x)" % (
                END.get(status, "status %d" % status), value >> 16, "ok" if value & 1 else "MISMATCH", low))
    return 0


if __name__ == '__main__':
    sys.exit(main())
