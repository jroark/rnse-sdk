#!/usr/bin/env python3
"""What examples/flashchk193 sent, from a CAN log.

    flashchk193.py LOG [LOG ...]

Prints the module directory of a 193's flash, each module's header and
whether the sum of what is in flash matches the header's, the first
bytes of the battery-backed memory, and which stretches of the flash are
erased.  The last run in the log is the one shown.  No packages needed.
"""
import re
import sys

LINE = re.compile(r'\b(?:rx|tx)\s+([0-9a-fA-F]{3})\s+\[(\d)\]((?:\s+[0-9a-fA-F]{2})*)')


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    runs = []
    for p in sys.argv[1:]:
        with open(p, errors='replace') as f:
            for line in f:
                m = LINE.search(line)
                if not m:
                    continue
                fid = int(m.group(1), 16)
                d = bytes(int(b, 16) for b in m.group(3).split())
                if len(d) != 8:
                    continue
                if fid == 0x7f8 and d[0] == 0xb0:
                    runs.append({'rec': {}, 'mem': {}})
                if not runs:
                    continue
                if fid == 0x7f8 and 0xb0 <= d[0] <= 0xb8:
                    runs[-1]['rec'][(d[0], d[1])] = (int.from_bytes(d[2:6], 'big'), int.from_bytes(d[6:8], 'big'))
                elif fid == 0x7f9:
                    off = int.from_bytes(d[0:3], 'big')
                    for k in range(5):
                        runs[-1]['mem'][off + k] = d[3 + k]
    if not runs:
        print("no run of flashchk193 in the log")
        return 1
    r = runs[-1]
    rec, mem = r['rec'], r['mem']
    print("run %d of %d%s" % (len(runs), len(runs), "" if (0xb8, 0) in rec else "  (NOT WHOLE: no end record)"))
    d = bytes(mem.get(o, 0) for o in range(512))
    have = sum(1 for o in range(512) if o in mem)
    print("module directory at 0x00400000 (%d of 512 bytes arrived):" % have)
    if all(b == 0xff for b in d):
        print("  erased (all 0xff)")
    elif all(b == 0 for b in d):
        print("  all zero")
    else:
        for o in range(0, 512, 16):
            row = d[o:o + 16]
            if any(row) and any(b != 0xff for b in row):
                print("  %03x  %s  %s" % (o, row.hex(' '), ''.join(chr(b) if 32 <= b < 127 else '.' for b in row)))
    i = 0
    while (0xb1, i) in rec:
        at, flags = rec[(0xb1, i)]
        if flags & 0x80:
            print("module %d: address %08x outside the flash" % (i, at))
            i += 1
            continue
        ver = b''.join(rec.get((t, i), (0, 0))[0].to_bytes(4, 'big') for t in (0xb2, 0xb3))
        ver = ''.join(chr(b) if 32 <= b < 127 else '.' for b in ver)
        size = rec.get((0xb4, i), (0, 0))[0]
        stored = rec.get((0xb5, i), (0, 0))[0]
        summed = rec.get((0xb6, i), (0, 0))[0]
        head = "header ok" if flags & 7 == 7 else "HEADER WRONG (flags %x)" % flags
        if flags & 8:
            verdict = "checksum ok" if stored == summed else "CHECKSUM WRONG"
        else:
            verdict = "size not sane, not summed"
        print("module %d at %08x: %s, version %s, size %08x, sum in header %08x, of the flash %08x: %s"
              % (i, at, head, ver, size, stored, summed, verdict))
        i += 1
    s = b''.join(rec[(0xb7, n)][0].to_bytes(4, 'big') for n in range(8) if (0xb7, n) in rec)
    print("battery-backed memory, first bytes: %s" % s.hex(' '))
    bits = [mem.get(0x10000 + k) for k in range(128)]
    if None in bits:
        print("erased map: not whole")
    else:
        erased = [bool(bits[n >> 3] & (0x80 >> (n & 7))) for n in range(1024)]
        print("flash, 64 KiB at a time (%d of 1024 erased):" % sum(erased))
        start = 0
        for n in range(1, 1025):
            if n == 1024 or erased[n] != erased[start]:
                print("  %08x-%08x  %s" % (start << 16, (n << 16) - 1, "erased" if erased[start] else "data"))
                start = n
    return 0


if __name__ == '__main__':
    sys.exit(main())
