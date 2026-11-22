#!/usr/bin/env python3
"""A bench 193 run from a script, with nobody at the bench: the Mac's end
of examples/mon193, the monitor a unit takes over CAN.  No packages
needed (tools/canbench.py's CANable code is used).

    bench193.py run TARGET SCRIPT OUT [options]
        TARGET is the CANable's serial device, or `emu:` and the options
        of tools/mon193emu.rs (`emu:--disc LOADER.iso`), an emulated 193
        run by cargo from this checkout.  The unit's starts are followed:
        a loader's start gets --loader's file if it is the disc's older
        loader (record 0x70 without bit 7), then --monitor's; a monitor's
        start (record 0xb1) is what the script waits for.  A monitor that
        stops answering is waited for (the watchdog starts the unit
        again, the pushes follow) and the script goes on with its next
        line.  A CANable that drops off the USB or answers no version is
        opened again.  Options:
          --loader FILE    a newer SD loader for the disc's older one
          --monitor FILE   out/mon193-193.bin (default: beside this kit)
          --ignition       0x271 [03] every 100 ms
          --log FILE       every frame, as canbench.py's log (default
                           OUT + '.log')
          --no-push        the monitor is running: push nothing at first
          --wait S         how long to wait for the unit at the start
                           (default 900)
    bench193.py diff A B
        Two runs' results side by side where they differ (a unit's and
        the emulator's of the same script).

The script, a line each ('#' starts a comment; numbers in hex):

    r8|r16|r32 ADDR [COUNT]     read COUNT values from ADDR on
    w8|w16|w32 ADDR VALUE       write (the monitor refuses the flash and
                                the battery-backed memory)
    block ADDR LEN [FILE]       LEN bytes; saved as FILE beside OUT
    info N                      the monitor's item N (examples/mon193)
    front on|off                the faceplate's messages into the log
    radio on|off                the radio processor's unasked frames too
    keepalive on|off            the monitor's keep-alive to the radio
                                processor (on from its start)
    wait MS                     (decimal)
    restart | hang | loader     and wait for the monitor to be back
    starve MS                   the watchdog not served for MS milliseconds
                                (decimal); LOST when it resets the unit
    cdinit | cdready MS         the kit's cd_init, cd_wait_ready: the result
    cdread SECTOR [N]           N sectors (up to 4) by cd_read: its result
                                and the CRC-32 of what it read
    caninit N                   the CAN controller set up from reset, variant
                                N (examples/mon193, can_try); LOST when it
                                leaves the bus silent (the unit restarts)
    push [FILE]                 a program to the running monitor (default:
                                the monitor's own file, a newer build)
    toradio CMD [HEX]           a frame to the radio processor (up to four
                                bytes of data) and its answer
    send ID HEX                 a frame of our own onto the bus
    note TEXT                   into OUT as it is

OUT gets a line for each value, `r32 ff000030 = 10300700`, `= FAULT 0e0`
for an exception, `= REFUSED`, `= LOST` when the monitor went away."""
import os
import subprocess
import select
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import canbench  # noqa: E402

KIT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REQUEST, ANSWER, FRONT, RADIO, BYTES = 0x7f2, 0x7f3, 0x7f4, 0x7f5, 0x7f9


class Emulator:
    """tools/mon193emu.rs as a bus: the same calls as canbench.Slcan."""

    def __init__(self, options):
        cmd = ['cargo', 'run', '--release', '--quiet', '--example', 'mon193emu']
        rnse = os.environ.get('RNSE_EMULATOR')
        if rnse:
            cmd += ['--config', 'patch."https://github.com/jroark/rnse".rnse.path="%s"' % rnse]
        self.p = subprocess.Popen(cmd + ['--'] + options, cwd=KIT, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.fd = self.p.stdout.fileno()
        self.buf = b''
        self.version = 'emulator'

    def send(self, fid, data):
        self.p.stdin.write(b't%03X%d%s\r' % (fid, len(data), data.hex().upper().encode()))
        self.p.stdin.flush()

    def frames(self, timeout):
        r, _, _ = select.select([self.fd], [], [], timeout)
        if r:
            got = os.read(self.fd, 65536)
            if not got:
                raise OSError('the emulator ended')
            self.buf += got
        out = []
        while b'\r' in self.buf:
            line, self.buf = self.buf.split(b'\r', 1)
            if line[:1] == b't':
                n = int(line[4:5], 16)
                out.append((int(line[1:4], 16), bytes.fromhex(line[5:5 + 2 * n].decode())))
        return out

    def close(self):
        self.p.kill()


class Gone(Exception):
    """The monitor went away while something was asked of it."""


class Bench:
    def __init__(self, target, log_path, loader, monitor, ignition, pushing):
        self.target = target
        self.log = canbench.Log(log_path)
        self.loader = loader
        self.monitor = monitor
        self.ignition = ignition
        self.bus = None
        self.next_ignition = 0.0
        self.alive = False       # a monitor answers
        self.seen = 0.0          # when it last said anything
        self.due = None          # 'loader' or 'monitor': a push owed
        self.due_at = 0.0
        self.pushing = False
        self.tries = 10
        self.seq = 0
        self.pending = []        # frames for whoever waits
        self.starts = 0

    # ---- the bus ------------------------------------------------------
    def open(self):
        while True:
            try:
                if self.target.startswith('emu:'):
                    self.bus = Emulator(self.target[4:].split())
                else:
                    self.bus = canbench.Slcan(self.target)
                    if not self.bus.version.strip(' ,'):
                        # Degraded (seen on the bench: frames lost, then
                        # deaf): opened again, it is itself.
                        self.log.note('CANable gives no version: opened again')
                        self.bus.close()
                        time.sleep(1)
                        continue
                self.log.note('bus: %s' % self.bus.version)
                return
            except OSError as e:
                self.log.note('no bus (%s): waiting' % e)
                time.sleep(3)

    def send(self, fid, data, quiet=False):
        try:
            self.bus.send(fid, data)
            self.log.frame('tx', fid, data, quiet=quiet)
        except OSError as e:
            self.lost_bus(e)

    def lost_bus(self, e):
        self.log.note('bus lost (%s)' % e)
        try:
            self.bus.close()
        except Exception:
            pass
        self.alive = False
        time.sleep(2)
        self.open()

    def poll(self, timeout=0.005):
        """Frames taken in, the unit's state followed; returns them."""
        now = time.time()
        if self.ignition and now >= self.next_ignition:
            self.next_ignition = now + 0.1
            self.send(0x271, b'\x03', quiet=True)
        try:
            frames = self.bus.frames(timeout)
        except OSError as e:
            self.lost_bus(e)
            return []
        for fid, d in frames:
            self.log.frame('rx', fid, d, quiet=fid not in (0x7f3, 0x7f4, 0x7f5, 0x7f6, 0x7f8))
            if fid == 0x7f8 and len(d) == 8:
                if d[0] == 0x70:
                    self.alive = False
                    if not d[1] & 0x80 and self.loader:
                        self.due = 'loader'
                    else:
                        self.due = 'monitor'
                    self.due_at = time.time() + 1.5
                    self.tries = 10
                elif d[0] == 0xb1:
                    self.alive = True
                    self.seen = time.time()
                    self.starts += 1
                    self.due = None
                    self.log.note('MONITOR started (version %d)' % d[1])
                elif d[0] == 0xb0:
                    if not self.alive:
                        self.log.note('MONITOR heard')
                    self.alive = True
                    self.seen = time.time()
            elif fid == ANSWER:
                self.seen = time.time()
        if self.alive and time.time() - self.seen > 8:
            self.log.note('MONITOR silent for 8 s')
            self.alive = False
        if self.due and not self.pushing and time.time() >= self.due_at:
            what, self.due = self.due, None
            path = self.loader if what == 'loader' else self.monitor
            result = self.push(open(path, 'rb').read())
            self.log.note('PUSH %s (%s): %s' % (path, what, result))
            if result.endswith('started'):
                self.tries = 10
            elif self.tries > 0:
                # Not taken (the loader busy at its slots, say): again
                # in a moment, ten times at most for one start.
                self.tries -= 1
                self.due, self.due_at = what, time.time() + 3
        return frames

    # ---- canload, as canbench.py's ------------------------------------
    def push_answers(self, timeout, want):
        end = time.time() + timeout
        while time.time() < end:
            for fid, d in self.poll(0.002):
                if fid == 0x7f8 and len(d) == 8 and 0xc0 <= d[0] <= 0xc2 and want(d):
                    return d
        return None

    def push(self, image):
        self.pushing = True
        try:
            size, crc = len(image), zlib.crc32(image) & 0xffffffff
            self.send(0x7f0, bytes([1, size >> 16 & 255, size >> 8 & 255, size & 255]) + crc.to_bytes(4, 'big'))
            r = self.push_answers(1.0, lambda d: d[0] == 0xc0)
            if r is None:
                return 'no program answered'
            if r[1] != 1:
                return 'refused (%02x)' % r[1]
            frames = (size + 6) // 7
            k, start, tries = 0, time.time(), 0
            while k < frames:
                end = min(k - k % 32 + 32, frames)
                for n in range(k, end):
                    self.send(0x7f1, bytes([n & 255]) + image[7 * n:7 * n + 7], quiet=True)
                want = min(7 * end, size)
                r = self.push_answers(1.0, lambda d: d[0] == 0xc1 and (d[1] == 0xff or int.from_bytes(d[2:6], 'big') >= want))
                if r is None:
                    tries += 1
                    if tries > 5:
                        self.send(0x7f0, b'\x04')
                        return 'no answer at frame %d' % k
                    continue
                tries = 0
                off = int.from_bytes(r[2:6], 'big')
                k = (off + 6) // 7 if off < size else frames
            self.send(0x7f0, b'\x03')
            r = self.push_answers(2.0, lambda d: d[0] == 0xc2)
            if r is None:
                return 'sent in %.1f s, no answer to the end' % (time.time() - start)
            return 'sent in %.1f s: %s' % (time.time() - start, {0: 'started', 1: 'CRC wrong', 2: 'not started'}.get(r[1], '?'))
        finally:
            self.pushing = False

    # ---- the monitor --------------------------------------------------
    def wait_monitor(self, timeout=600):
        end = time.time() + timeout
        said = time.time()
        while not self.alive:
            self.poll(0.02)
            if time.time() > end:
                raise SystemExit('no monitor after %d s: is the unit on (PSU, POWER, PIN) with the loader disc in?' % timeout)
            if time.time() - said > 30:
                said = time.time()
                self.log.note('waiting for the monitor')

    def ask(self, op, payload, count=1, timeout=2.0, collect=None, tries=3):
        """A request; its `count` answers as (status, value, x, y)."""
        for attempt in range(tries):
            if not self.alive:
                raise Gone()
            self.seq = self.seq % 255 + 1
            self.send(REQUEST, bytes([self.seq, op]) + payload)
            got = []
            end = time.time() + timeout
            while time.time() < end and self.alive:
                for fid, d in self.poll(0.002):
                    if fid == BYTES and collect is not None:
                        collect(d)
                        end = time.time() + timeout
                    if fid == ANSWER and len(d) == 8 and d[0] == self.seq:
                        got.append((d[1], int.from_bytes(d[2:6], 'big'), d[6], d[7]))
                        # The rest of a row follows within milliseconds;
                        # one that does not was lost.
                        end = time.time() + min(timeout, 0.3 if collect is None else timeout)
                        if len(got) >= count or d[1] != 0:
                            return got
            if got:
                return got
            self.log.note('no answer to request %02x (try %d)' % (op, attempt + 1))
        self.alive = False
        raise Gone()


def number(text):
    return int(text.replace('_', ''), 16)


def status_text(st, value):
    return {1: 'FAULT %03x' % value, 2: 'REFUSED', 3: 'NOT UNDERSTOOD'}.get(st, '?%d' % st)


def run(argv):
    target, script_path, out_path = argv[0], argv[1], argv[2]
    opts = argv[3:]
    loader = None
    monitor = os.path.join(KIT, 'out', 'mon193-193.bin')
    log_path = out_path + '.log'
    ignition = False
    pushing = True
    patience = 900.0
    i = 0
    while i < len(opts):
        o = opts[i]
        if o == '--loader':
            i += 1
            loader = opts[i]
        elif o == '--monitor':
            i += 1
            monitor = opts[i]
        elif o == '--log':
            i += 1
            log_path = opts[i]
        elif o == '--ignition':
            ignition = True
        elif o == '--no-push':
            pushing = False
        elif o == '--wait':
            i += 1
            patience = float(opts[i])
        else:
            sys.exit('unknown option %s' % o)
        i += 1
    lines = []
    for line in open(script_path):
        line = line.split('#', 1)[0].strip()
        if line:
            lines.append(line)
    b = Bench(target, log_path, loader, monitor, ignition, pushing)
    b.open()
    out = open(out_path, 'w', buffering=1)

    def say(text):
        out.write(text + '\n')
        print('    ' + text, flush=True)

    # A loader already waiting (its start missed) takes a push as well:
    # try one when nothing has been heard for a few seconds.
    began = t0 = time.time()
    while not b.alive:
        b.poll(0.02)
        if pushing and not b.due and time.time() - began > 5:
            b.due, b.due_at, b.tries = 'monitor', time.time(), 0
            began = time.time() + 25
        if time.time() - t0 > patience:
            sys.exit('no monitor: is the unit on (PSU, POWER, PIN) with the loader disc in?')
    try:
        for line in lines:
            w = line.split()
            cmd = w[0]
            try:
                if not b.alive and cmd not in ('note', 'wait'):
                    b.wait_monitor()
                if cmd in ('r8', 'r16', 'r32'):
                    size = int(cmd[1:]) // 8
                    addr = number(w[1])
                    count = number(w[2]) if len(w) > 2 else 1
                    done = 0
                    while done < count:
                        n = min(count - done, 255)
                        a = addr + done * size
                        got = b.ask(1, a.to_bytes(4, 'big') + bytes([size, n]), count=n)
                        # An answer lost on the way (a unit loses one in
                        # thirty of a long row): asked for again, alone.
                        if got[-1][0] == 0 and len(got) < n:
                            have = {g[2]: g for g in got}
                            for k in range(n):
                                if k not in have:
                                    st, value, _, _ = b.ask(1, (a + k * size).to_bytes(4, 'big') + bytes([size, 1]))[0]
                                    have[k] = (st, value, k, size)
                            got = [have[k] for k in sorted(have)]
                        for st, value, index, _ in got:
                            at = a + index * size
                            say('%s %08x = %s' % (cmd, at, ('%0*x' % (2 * size, value)) if st == 0 else status_text(st, value)))
                        if got[-1][0] != 0 or len(got) < n:
                            break
                        done += n
                elif cmd in ('w8', 'w16', 'w32'):
                    size = int(cmd[1:]) // 8
                    addr, value = number(w[1]), number(w[2])
                    b.ask(2, value.to_bytes(4, 'big') + b'\0\0')
                    st, v, _, _ = b.ask(3, addr.to_bytes(4, 'big') + bytes([size, 0]))[0]
                    say('%s %08x %0*x = %s' % (cmd, addr, 2 * size, value, 'ok' if st == 0 else status_text(st, v)))
                elif cmd == 'block':
                    addr, length = number(w[1]), number(w[2])
                    data = bytearray()
                    bad = None
                    while len(data) < length and bad is None:
                        n = min(length - len(data), 0x8000)
                        part = bytearray(n)

                        def collect(d, part=part, n=n):
                            off = int.from_bytes(d[0:3], 'big')
                            part[off:off + 5] = d[3:3 + max(0, min(5, n - off))]
                        st, value, _, _ = b.ask(4, (addr + len(data)).to_bytes(4, 'big') + n.to_bytes(2, 'big'), timeout=3.0, collect=collect)[0]
                        if st != 0:
                            bad = status_text(st, value)
                        elif zlib.crc32(bytes(part)) & 0xffffffff != value:
                            bad = 'CRC WRONG'
                        else:
                            data += part
                    if bad:
                        say('block %08x %x = %s' % (addr, length, bad))
                    else:
                        say('block %08x %x = crc %08x' % (addr, length, zlib.crc32(bytes(data)) & 0xffffffff))
                        for o in range(0, length, 16):
                            say('  %08x: %s' % (addr + o, data[o:o + 16].hex(' ')))
                        if len(w) > 3:
                            open(os.path.join(os.path.dirname(os.path.abspath(out_path)), w[3]), 'wb').write(data)
                elif cmd == 'info':
                    item = number(w[1])
                    st, value, _, _ = b.ask(5, bytes([item, 0, 0, 0, 0, 0]))[0]
                    say('info %x = %s' % (item, '%08x' % value if st == 0 else status_text(st, value)))
                elif cmd in ('front', 'radio', 'keepalive'):
                    b.ask({'front': 8, 'radio': 9, 'keepalive': 12}[cmd], bytes([1 if w[1] == 'on' else 0, 0, 0, 0, 0, 0]))
                    say('%s %s' % (cmd, w[1]))
                elif cmd == 'wait':
                    end = time.time() + int(w[1]) / 1000
                    while time.time() < end:
                        b.poll(0.01)
                elif cmd in ('restart', 'hang', 'loader'):
                    op, word = {'restart': (6, b'RST!'), 'hang': (7, b'HANG'), 'loader': (10, b'BACK')}[cmd]
                    starts, t = b.starts, time.time()
                    try:
                        # (Asked once: the unit may be gone before its
                        # answer is out.)
                        st = b.ask(op, word + b'\0\0', timeout=1.0, tries=1)[0][0]
                    except Gone:
                        st = 0
                    if st != 0:
                        say('%s = REFUSED' % cmd)
                        continue
                    b.alive = False
                    b.log.note('%s: waiting for the monitor to be back' % cmd.upper())
                    while b.starts == starts:
                        b.poll(0.02)
                        if time.time() - t > 600:
                            say('%s = NOT BACK after 600 s' % cmd)
                            raise SystemExit(1)
                    say('%s = back' % cmd)
                    b.log.note('%s: back after %.1f s' % (cmd.upper(), time.time() - t))
                elif cmd == 'starve':
                    ms = int(w[1])
                    b.ask(15, ms.to_bytes(4, 'big') + b'\0\0', timeout=ms / 1000 + 2, tries=1)
                    say('%s = survived' % line)
                elif cmd in ('cdinit', 'cdready'):
                    arg = int(w[1]) if len(w) > 1 else 0
                    st, value, _, _ = b.ask(14, arg.to_bytes(4, 'big') + bytes([0, 1 if cmd == 'cdinit' else 2]), timeout=arg / 1000 + 15, tries=1)[0]
                    say('%s = %d' % (line, value - (1 << 32) if value >> 31 else value))
                elif cmd == 'cdread':
                    sector, n = number(w[1]), (number(w[2]) if len(w) > 2 else 1)
                    got = b.ask(14, sector.to_bytes(4, 'big') + bytes([n, 0]), count=4, timeout=20, tries=1)
                    r = [g[1] for g in got]
                    res = r[0] - (1 << 32) if r[0] >> 31 else r[0]
                    say('cdread %x %x = %d, crc %08x' % (sector, n, res, r[1]) if len(r) == 4 else '%s = ?' % line)
                    if len(r) == 4:
                        b.log.note('cdread %x: %d us, buffer at %08x' % (sector, r[2], r[3]))
                elif cmd == 'caninit':
                    st, value, variant, tries = b.ask(13, bytes([number(w[1]), 0, 0, 0, 0, 0]), timeout=3.0, tries=1)[0]
                    say('%s = works: status %04x after the reset, %04x running, answer at try %d' % (line, value >> 16, value & 0xffff, tries + 1))
                elif cmd == 'push':
                    # The monitor's file (or the one named) to the
                    # running monitor, which starts it in its place.
                    starts = b.starts
                    result = b.push(open(w[1] if len(w) > 1 else b.monitor, 'rb').read())
                    t = time.time()
                    while b.starts == starts and time.time() - t < 10:
                        b.poll(0.02)
                    say('push = %s' % ('started' if b.starts != starts else result))
                elif cmd == 'toradio':
                    data = bytes.fromhex(''.join(w[2:]))
                    st, value, r, got = b.ask(11, bytes([number(w[1]), len(data)]) + data.ljust(4, b'\0'), timeout=4.0, tries=1)[0]
                    r = r - 256 if r > 127 else r
                    say('%s = %s' % (line, 'no answer (%d)' % r if r < 0 else 'answer %02x [%s]' % (got, value.to_bytes(4, 'big')[:r].hex(' '))))
                elif cmd == 'send':
                    b.send(number(w[1]), bytes.fromhex(''.join(w[2:])))
                    say(line)
                elif cmd == 'note':
                    say(line)
                else:
                    sys.exit('script: %s?' % line)
            except Gone:
                say('%s = LOST' % line)
    except KeyboardInterrupt:
        pass
    finally:
        b.log.note('run ended')
        b.bus.close()


def diff(argv):
    def load(path):
        d, order = {}, []
        for line in open(path):
            line = line.rstrip('\n')
            if ' = ' in line and not line.startswith(' '):
                k, v = line.split(' = ', 1)
                if k not in d:
                    order.append(k)
                d[k] = v
        return d, order
    a, order = load(argv[0])
    b, _ = load(argv[1])
    n = 0
    for k in order:
        if k in b and a[k] != b[k]:
            print('%-24s %-22s %s' % (k, a[k], b[k]))
            n += 1
    only = [k for k in order if k not in b]
    print('%d of %d differ%s' % (n, len(order), (', %d only in %s' % (len(only), argv[0])) if only else ''))


def main():
    if len(sys.argv) >= 5 and sys.argv[1] == 'run':
        run(sys.argv[2:])
    elif len(sys.argv) == 4 and sys.argv[1] == 'diff':
        diff(sys.argv[2:])
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
