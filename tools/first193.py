#!/usr/bin/env python3
"""What examples/first193 sent, put back together from a CAN log.

    first193.py LOG [LOG ...] [--out DIR]

LOG: a log of tools/canbench.py ("rx 7f8 [8] .."), or the emulator's
--trace-can output ("[can] tx 7f8 [8] .."), with one run of the
program or several (each begins with record 0xf0).  A dump is taken
from a single run, the first one in which it arrived whole and matches
the CRC that run sent: the battery-backed memory changes from one start
of the unit to the next, so runs are not mixed.  Prints the records (0x7F8) test by test
and, with --out, writes the dumps (0x7F9/0x7FA) there: sram.bin
(256 KiB), params.bin (the top 128 KiB of the flash, 0x03fe0000) and
bootrom.bin, each checked against the CRC-32 the unit sent.  Missing
bytes are reported, not invented.  No packages needed.
"""
import os
import re
import sys
import zlib

LINE = re.compile(r'\b(?:rx|tx)\s+([0-9a-fA-F]{3})\s+\[(\d)\]((?:\s+[0-9a-fA-F]{2})*)')
STAMP = re.compile(r'^\s*([0-9]+\.[0-9]+)\s')
REGIONS = {0: ('bootrom.bin', 'boot ROM'), 1: ('sram.bin', 'battery-backed memory'), 2: ('params.bin', 'flash parameters')}
TESTS = {0x20: 'registers', 0x21: 'register addresses', 0x22: 'start state', 1: 'processor', 13: 'flash banks',
         14: 'flash below the software', 2: 'A/D converter', 3: 'ports', 5: 'faceplate', 7: 'drive', 8: 'CAN',
         9: 'keys and knobs', 18: 'dumps', 0xf0: 'started', 0xff: 'end of run'}


def frames(paths):
    """(time or None, identifier, data) of every frame."""
    for p in paths:
        with open(p, errors='replace') as f:
            for line in f:
                m = LINE.search(line)
                if m:
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
    runs = []          # each: records, memory, sizes, crcs
    def new_run():
        runs.append({'recs': [], 'mem': {}, 'sizes': {}, 'crcs': {}})
    new_run()
    last_t = None
    for t, fid, d in frames(args):
        # A unit that starts again is seconds off the bus (canbench's
        # times); a run also begins with its record 0xf0.
        if t is not None and last_t is not None and t - last_t > 5 and runs[-1]['recs']:
            new_run()
        if t is not None and fid in (0x7f8, 0x7f9, 0x7fa):
            last_t = t
        run = runs[-1]
        if fid == 0x7f8 and len(d) == 8:
            test, item = d[0], d[1]
            value = int.from_bytes(d[2:6], 'big')
            second = int.from_bytes(d[6:8], 'big')
            if test == 0xf0 and any(r[0] != 0xf0 and r[0] < 0x70 for r in run['recs']):
                new_run()
                run = runs[-1]
            run['recs'].append((test, item, value, second))
            if test == 18 and item == 0:
                run['sizes'][value] = second << 8
            if test == 18 and item == 3:
                run['crcs'][second] = value
        elif fid == 0x7f9 and len(d) > 3:
            a = int.from_bytes(d[0:3], 'big')
            r, o = a >> 22, a & 0x3fffff
            m = run['mem'].setdefault(r, {})
            for k, b in enumerate(d[3:]):
                m[o + k] = b
        elif fid == 0x7fa and len(d) == 4:
            a = int.from_bytes(d[0:3], 'big')
            r, o = a >> 22, a & 0x3fffff
            m = run['mem'].setdefault(r, {})
            for k in range(256):
                m[o + k] = d[3]

    # The records, from the run that has the most of them.
    best = max(runs, key=lambda r: len(r['recs']))
    print(f'{len(runs)} run(s) in the log; records from the fullest one')
    seen = set()
    last = None
    regs = {}
    for test, item, value, second in best['recs']:
        key = (test, item, value, second)
        if key in seen:
            continue
        seen.add(key)
        if test == 0x21:
            regs[item] = value
            continue
        if test != last:
            print(f'== test {test:#x} {TESTS.get(test, "")}')
            last = test
        if test == 0x20 and item in regs:
            print(f'   {regs[item]:08x} = {value:08x}')
            continue
        if test == 7 and item < 24:
            print(f'   {item:3d}  {value:08x}  {value.to_bytes(4, "big").decode("latin1")!r}')
            continue
        print(f'   item {item:#04x}  {value:08x} ({value})  {second:04x}')

    for r, (name, what) in sorted(REGIONS.items()):
        chosen = None
        for n, run in enumerate(runs):
            size = run['sizes'].get(r)
            m = run['mem'].get(r, {})
            if size is None:
                continue
            got = sum(1 for o in range(size) if o in m)
            data = bytes(m.get(o, 0) for o in range(size))
            crc = zlib.crc32(data) & 0xffffffff
            ok = got == size and run['crcs'].get(r) == crc
            print(f'{what}, run {n}: {got} of {size} bytes, CRC {crc:08x} against the unit\'s {run["crcs"].get(r, 0):08x}: {"good" if ok else "incomplete or wrong"}')
            if ok and chosen is None:
                chosen = data
        if chosen is None:
            # Runs that sent the same CRC for this region sent the same
            # bytes (the boot ROM and the flash never change between
            # starts; the battery-backed memory does): their frames may
            # fill each other's gaps.
            groups = {}
            sizes = [run['sizes'][r] for run in runs if r in run['sizes']]
            for run in runs:
                if r in run['crcs']:
                    groups.setdefault(run['crcs'][r], []).append(run)
            for want, group in groups.items():
                if len(group) < 2 or not sizes:
                    continue
                size = max(sizes)
                m = {}
                for run in group:
                    m.update(run['mem'].get(r, {}))
                got = sum(1 for o in range(size) if o in m)
                data = bytes(m.get(o, 0) for o in range(size))
                crc = zlib.crc32(data) & 0xffffffff
                print(f'{what}, {len(group)} runs with CRC {want:08x} together: {got} of {size} bytes, CRC {crc:08x}: {"good" if got == size and crc == want else "incomplete or wrong"}')
                if got == size and crc == want:
                    chosen = data
                    break
        if chosen is None:
            print(f'{what}: no complete run')
        elif out:
            os.makedirs(out, exist_ok=True)
            with open(os.path.join(out, name), 'wb') as f:
                f.write(chosen)
            print(f'{what}: written to {os.path.join(out, name)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
