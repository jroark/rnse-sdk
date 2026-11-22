#!/usr/bin/env python3
"""What examples/dump193 sent, put back together from a CAN log.

    dump193.py LOG [LOG ...] [--out FILE]

LOG: a log of tools/canbench.py ("rx 7f8 [8] ..") or the emulator's
--trace-can output ("[can] tx 7f8 [8] .."), with one run of the program
or several (each begins with record 0xd0).  dump193 reads one window of
memory through P2 and sends it: records on 0x7F8 (0xd0 alive with the
base, 0xd1 the window's base and size, 0xd2 the CRC-32, 0xff the end)
and the bytes on 0x7F9 (three bytes of offset, five of data).  This takes
the window from a single run, the first in which it arrived whole and
matched the CRC that run sent.  Prints each run's base, size and CRC and,
with --out, writes the window there.  Missing bytes are reported, not
invented.  No packages needed.
"""
import re
import sys
import zlib

LINE = re.compile(r'\b(?:rx|tx)\s+([0-9a-fA-F]{3})\s+\[(\d)\]((?:\s+[0-9a-fA-F]{2})*)')
STAMP = re.compile(r'^\s*([0-9]+\.[0-9]+)\s')


def frames(paths):
    for p in paths:
        with open(p, errors='replace') as f:
            for line in f:
                m = LINE.search(line)
                if not m:
                    continue
                data = bytes(int(b, 16) for b in m.group(3).split())
                t = STAMP.match(line)
                yield (float(t.group(1)) if t else None), int(m.group(1), 16), data[:int(m.group(2))]


def main():
    args = sys.argv[1:]
    out = None
    if '--out' in args:
        i = args.index('--out')
        out = args[i + 1]
        del args[i:i + 2]
    if not args:
        print(__doc__)
        return 1

    runs = []

    def new_run():
        runs.append({'base': None, 'size': None, 'crc': None, 'mem': {}})

    new_run()
    last_t = None
    for t, fid, d in frames(args):
        if t is not None and last_t is not None and t - last_t > 5 and runs[-1]['mem']:
            new_run()
        last_t = t
        r = runs[-1]
        if fid == 0x7f8 and len(d) == 8:
            test = d[0]
            value = int.from_bytes(d[2:6], 'big')
            second = int.from_bytes(d[6:8], 'big')
            if test == 0xd0 and r['mem']:
                new_run()
                r = runs[-1]
            if test == 0xd0:
                r['base'] = value
            elif test == 0xd1:
                r['base'] = value
                r['size'] = second << 4
            elif test == 0xd2:
                r['crc'] = value
        elif fid == 0x7f9 and len(d) == 8:
            off = int.from_bytes(d[0:3], 'big')
            for k in range(5):
                r['mem'][off + k] = d[3 + k]

    # The first whole, CRC-matching run; else the most complete.
    best = None
    for n, r in enumerate(runs):
        if r['size'] is None:
            continue
        have = sum(1 for o in range(r['size']) if o in r['mem'])
        window = bytes(r['mem'].get(o, 0) for o in range(r['size']))
        ok = have == r['size'] and r['crc'] == (zlib.crc32(window) & 0xffffffff)
        base = r['base'] or 0
        print("run %d: base 0x%08x, %d of %d bytes, CRC %s"
              % (n, base, have, r['size'],
                 "ok" if ok else ("mismatch (sent 0x%08x)" % r['crc'] if r['crc'] is not None else "none sent")))
        if ok and best is None:
            best = (base, window)
        if best is None and (best is None or have):
            best = best or (base, window)
    if best is None:
        print("no complete window")
        return 1
    base, window = best
    if out:
        with open(out, 'wb') as f:
            f.write(window)
        print("wrote %d bytes of 0x%08x to %s" % (len(window), base, out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
