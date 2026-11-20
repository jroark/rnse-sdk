#!/usr/bin/env python3
"""The Mac's end of the car's CAN bus, through a CANable 2 with its
slcan firmware (https://github.com/normaldotcom/canable2-fw): logs the
bus, plays the car and the modules the unit talks to, and checks the
kit's CAN code on a unit (examples/canbench).  No packages needed.

    canbench.py run PORT LOG [options]
        Log every frame (rx: from the bus, tx: ours) with the time, and
        act as other nodes meanwhile.  Options:
          --listen             listen only: acknowledge nothing, send nothing
          --ignition           the ignition lock message, 0x271 [03] every 100 ms
          --send ID:HEX[@MS]   a frame once, or every MS milliseconds
          --peer FROM:TO       the far end of a TP2.0 channel the unit opens
                               on FROM (6c0: the cluster, 6c4: the telephone),
                               answered on TO; blocks are printed
          --nm NODE            take part in network management as NODE (hex):
                               an alive message at the start, and the ring
                               handed back 100 ms after it is handed to us
                               (OSEK layout: byte 0 the node addressed,
                               byte 1 1 alive, 2 ring, 4 limp home, 0x10
                               sleep indication, 0x20 sleep acknowledged)
          --script FILE        timed lines: "MS ID:HEX[@PERIOD]", "MS stop ID",
                               "MS mark TEXT" (MS from the start)
          --for S              stop after S seconds
    canbench.py bench PORT LOG
        The check of examples/canbench running on the unit: waits for its
        state frame, sends frames of every length at rising rates, checks
        each one's answer on 0x7f8, asks for a burst and counts it.
    canbench.py decode LOG
        A log again, with what is known of each identifier.

PORT is the CANable's serial device (/dev/cu.usbmodem... on a Mac).  The
bus is 100 kbit/s, the infotainment bus of the cars the RNS-E is in: a
low-speed, fault-tolerant one (the unit's transceiver is a TJA1054).
The CANable's 120 ohm termination must be off on it."""
import os
import select
import sys
import termios
import time

ECHO = 0x7f8
ORDERS = 0x7f7


def fnv24(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h & 0xFFFFFF


class Slcan:
    def __init__(self, dev, listen=False):
        self.fd = os.open(dev, os.O_RDWR | os.O_NOCTTY)
        a = termios.tcgetattr(self.fd)
        a[0] = 0
        a[1] = 0
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        a[3] = 0
        a[4] = a[5] = termios.B115200
        a[6][termios.VMIN] = 0
        a[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        self.buf = b''
        self.listen = listen
        # Configuration only with the channel closed: close, 100 kbit/s,
        # normal or silent mode, automatic retransmission, open.
        for cmd in (b'C', b'S3', b'M1' if listen else b'M0', b'A1', b'O'):
            os.write(self.fd, cmd + b'\r')
            time.sleep(0.05)
        self.version = self.ask(b'V')

    def ask(self, cmd):
        os.write(self.fd, cmd + b'\r')
        end = time.time() + 0.3
        out = b''
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.05)
            if r:
                out += os.read(self.fd, 256)
        # Frames that arrived meanwhile are kept for frames().
        lines = out.split(b'\r')
        self.buf += b'\r'.join(l for l in lines if l[:1] in (b't', b'T')) + b'\r'
        return b' '.join(l for l in lines if l and l[:1] not in (b't', b'T')).decode('ascii', 'replace')

    def send(self, fid, data):
        if self.listen:
            return
        os.write(self.fd, b't%03X%d%s\r' % (fid, len(data), data.hex().upper().encode()))

    def frames(self, timeout):
        """Frames received, as (id, bytes), waiting at most timeout s."""
        r, _, _ = select.select([self.fd], [], [], timeout)
        if r:
            self.buf += os.read(self.fd, 4096)
        out = []
        while b'\r' in self.buf:
            line, self.buf = self.buf.split(b'\r', 1)
            line = line.lstrip(b'\n\x07')
            try:
                if line[:1] == b't':
                    n = int(line[4:5], 16)
                    out.append((int(line[1:4], 16), bytes.fromhex(line[5:5 + 2 * n].decode())))
                elif line[:1] == b'T':
                    n = int(line[9:10], 16)
                    out.append((int(line[1:9], 16) | 0x80000000, bytes.fromhex(line[10:10 + 2 * n].decode())))
            except ValueError:
                pass
        return out

    def close(self):
        try:
            os.write(self.fd, b'C\r')
        finally:
            os.close(self.fd)


def describe(fid, d):
    """What is known of a frame (rnse docs/hardware.md, docs/telephone.md)."""
    if fid == ECHO and d:
        k = d[:1]
        if k == b'R' and len(d) == 8:
            return 'canbench: got #%d %03x [%d] hash %06x' % (d[1], d[2] << 8 | d[3], d[4], int.from_bytes(d[5:8], 'big'))
        if k == b'S' and len(d) == 8:
            return 'canbench: GSR %04x TEC %d REC %d lost %d fails %d' % (d[1] << 8 | d[2], d[3], d[4], d[5] << 8 | d[6], d[7])
        if k == b'B' and len(d) == 3:
            return 'canbench: burst %d frame %d' % (d[1], d[2])
        return 'kit record'
    if fid in (0x271, 0x2c3) and d:
        return 'ignition: key %s, %s' % ('in' if d[0] & 1 else 'out', 'on' if d[0] & 2 else 'off')
    if fid == 0x436:
        return 'unit network management'
    if 0x400 <= fid <= 0x43f:
        return 'network management, node %02x' % (fid & 0x3f)
    if fid == 0x661:
        return 'unit status'
    if fid in (0x6c0, 0x6c1, 0x6c4, 0x6c5) and d:
        who = {0x6c0: 'unit>cluster', 0x6c1: 'cluster>unit', 0x6c4: 'unit>telephone', 0x6c5: 'telephone>unit'}[fid]
        op = d[0]
        what = {0xa0: 'channel?', 0xa1: 'channel parameters', 0xa3: 'still there?', 0xa8: 'disconnect'}.get(op)
        if what is None:
            if op >> 4 == 0xb:
                what = 'ack, next %d' % (op & 15)
            elif op >> 4 == 0x9:
                what = 'not ready, next %d' % (op & 15)
            elif op < 0x40:
                what = 'data #%d%s%s' % (op & 15, ', last' if op & 0x10 else '', '' if op & 0x20 else ', ack wanted')
            else:
                what = '?'
        return 'TP2.0 %s %s' % (who, what)
    return ''


class Peer:
    """The far end of a TP2.0 channel (rnse src/dev/tp20.rs)."""

    def __init__(self, from_unit, to_unit):
        self.from_unit, self.to_unit = from_unit, to_unit
        self.open = False
        self.rx_seq = self.tx_seq = 0
        self.partial = b''
        self.blocks = []

    def frame(self, d):
        if not d:
            return []
        op = d[0]
        if op == 0xa0:
            self.open, self.rx_seq, self.tx_seq, self.partial = True, 0, 0, b''
            return [bytes([0xa1]) + d[1:]]
        if op == 0xa3:
            return [bytes([0xa1, 0x0f, 0x8a, 0xff, 0x4a, 0xff])]
        if op == 0xa8:
            was, self.open = self.open, False
            return [b'\xa8'] if was else []
        if op < 0x40 and self.open:
            if op & 15 != self.rx_seq:
                return [bytes([0xb0 | self.rx_seq])]
            self.rx_seq = (self.rx_seq + 1) & 15
            self.partial += d[1:]
            if op & 0x10:
                self.blocks.append(self.partial)
                self.partial = b''
            return [] if op & 0x20 else [bytes([0xb0 | self.rx_seq])]
        return []


def parse_frame(text):
    """ID:HEX[@MS] -> (id, bytes, period_ms or None)."""
    period = None
    if '@' in text:
        text, p = text.split('@', 1)
        period = int(p)
    fid, data = text.split(':', 1)
    return int(fid, 16), bytes.fromhex(data), period


class Log:
    def __init__(self, path):
        self.out = open(path, 'a', buffering=1)
        self.start = time.time()
        self.note('log started %s' % time.strftime('%Y-%m-%d %H:%M:%S'))

    def t(self):
        return time.time() - self.start

    def note(self, text):
        line = '%10.4f --- %s' % (self.t(), text)
        self.out.write(line + '\n')
        print(line, flush=True)

    def frame(self, way, fid, d, quiet=False):
        what = describe(fid, d)
        line = '%10.4f %s %03x [%d] %s%s' % (self.t(), way, fid, len(d), d.hex(' '), ('  ; ' + what) if what else '')
        self.out.write(line + '\n')
        if not quiet:
            print(line, flush=True)


def run(argv):
    dev, path = argv[0], argv[1]
    opts = argv[2:]
    listen = '--listen' in opts
    cyclic = {}          # id -> [data, period s, due]
    once = []
    peers = []
    script = []
    stop_at = None
    nm = None
    nm_due = []          # (time, data) of our ring messages
    i = 0
    while i < len(opts):
        o = opts[i]
        if o == '--ignition':
            cyclic[0x271] = [b'\x03', 0.1, 0]
        elif o == '--send':
            i += 1
            fid, data, period = parse_frame(opts[i])
            if period:
                cyclic[fid] = [data, period / 1000, 0]
            else:
                once.append((fid, data))
        elif o == '--peer':
            i += 1
            a, b = opts[i].split(':')
            peers.append(Peer(int(a, 16), int(b, 16)))
        elif o == '--nm':
            i += 1
            nm = int(opts[i], 16)
        elif o == '--script':
            i += 1
            for line in open(opts[i]):
                line = line.split('#', 1)[0].strip()
                if line:
                    ms, rest = line.split(None, 1)
                    script.append((int(ms) / 1000, rest))
            script.sort(key=lambda s: s[0])
        elif o == '--for':
            i += 1
            stop_at = float(opts[i])
        elif o != '--listen':
            sys.exit('unknown option %s' % o)
        i += 1
    bus = Slcan(dev, listen)
    log = Log(path)
    log.note('CANable %s, %s' % (bus.version, 'listen only' if listen else 'acknowledging'))

    def send(fid, data):
        if not listen:
            bus.send(fid, data)
            log.frame('tx', fid, data, quiet=fid in cyclic)

    for fid, data in once:
        send(fid, data)
    if nm is not None:
        send(0x400 | nm, bytes([nm, 0x01, 0, 0, 0, 0]))
    try:
        while stop_at is None or log.t() < stop_at:
            now = log.t()
            while script and script[0][0] <= now:
                _, cmd = script.pop(0)
                if cmd.startswith('mark '):
                    log.note('MARK ' + cmd[5:])
                elif cmd.startswith('stop '):
                    cyclic.pop(int(cmd[5:], 16), None)
                    log.note('stopped %s' % cmd[5:])
                else:
                    fid, data, period = parse_frame(cmd)
                    if period:
                        cyclic[fid] = [data, period / 1000, now]
                    else:
                        send(fid, data)
            for fid, c in cyclic.items():
                if c[2] <= now:
                    c[2] = now + c[1]
                    send(fid, c[0])
            while nm_due and nm_due[0][0] <= now:
                send(0x400 | nm, nm_due.pop(0)[1])
            for fid, d in bus.frames(0.005):
                log.frame('rx', fid, d)
                if nm is not None and 0x400 <= fid <= 0x43f and len(d) >= 2 and fid != 0x400 | nm:
                    sender = fid & 0x3f
                    if d[0] == nm and d[1] & 0x03 == 0x02:
                        # The ring is ours: on to the sender, the only
                        # other node there is.
                        nm_due.append((log.t() + 0.1, bytes([sender, 0x02 | (d[1] & 0x30 and 0x20), 0, 0, 0, 0])))
                    elif d[1] & 0x04 and not nm_due:
                        # Limp home: say we are here.
                        nm_due.append((log.t() + 0.02, bytes([sender, 0x01, 0, 0, 0, 0])))
                for p in peers:
                    if fid == p.from_unit:
                        n = len(p.blocks)
                        for answer in p.frame(d):
                            send(p.to_unit, answer)
                        for b in p.blocks[n:]:
                            log.note('BLOCK %03x: %s' % (fid, b.hex(' ')))
    except KeyboardInterrupt:
        pass
    finally:
        log.note('log ended')
        bus.close()


def bench(argv):
    dev, path = argv[0], argv[1]
    bus = Slcan(dev)
    log = Log(path)
    log.note('CANable %s; waiting for canbench on the unit' % bus.version)
    states = []

    def pump(seconds, until=None):
        got = []
        end = time.time() + seconds
        while time.time() < end:
            for fid, d in bus.frames(0.005):
                log.frame('rx', fid, d, quiet=fid == ECHO and d[:1] == b'R')
                if fid == ECHO and d[:1] == b'S' and len(d) == 8:
                    states.append(d)
                got.append((fid, d))
                if until and until(fid, d):
                    return got
        return got

    if not any(f == ECHO and d[:1] == b'S' for f, d in pump(15, lambda f, d: f == ECHO and d[:1] == b'S')):
        log.note('no state frame from the unit in 15 s: is canbench running, is the bus wired?')
        bus.close()
        return 1
    bus.send(ORDERS, b'\x02')
    log.frame('tx', ORDERS, b'\x02')
    pump(0.5)
    results = []
    # Frames of every length, then rows at rising rates: each the unit
    # must answer with its identifier, length and hash.  Answers are
    # collected while a row is being sent as well as after it.
    for gap_ms in (50, 20, 10, 5, 2, 0):
        sent, got = [], []
        before = states[-1] if states else None
        for n in range(40):
            length = n % 9
            fid = 0x100 + (n * 37) % 0x500
            data = bytes(((n * 29 + k * 11 + gap_ms) & 0xff) for k in range(length))
            bus.send(fid, data)
            log.frame('tx', fid, data, quiet=True)
            sent.append((fid, data))
            got += pump(gap_ms / 1000)
        got += pump(1.5)
        answers = [d[2:] for f, d in got if f == ECHO and d[:1] == b'R' and len(d) == 8]
        n_answers = len(answers)
        good = 0
        for fid, data in sent:
            want = bytes([fid >> 8, fid & 0xff, len(data)]) + fnv24(data).to_bytes(3, 'big')
            if want in answers:
                answers.remove(want)
                good += 1
        lost = (states[-1][5] << 8 | states[-1][6]) - ((before[5] << 8 | before[6]) if before else 0) if states else -1
        results.append((gap_ms, good, len(sent), n_answers))
        log.note('gap %2d ms: %d of %d answered right (%d answers; the unit lost %d)' % (gap_ms, good, len(sent), n_answers, lost))
    # Bursts of sixteen, back to back from the unit.
    for k in range(5):
        before = states[-1][7] if states else 0
        bus.send(ORDERS, b'\x01')
        log.frame('tx', ORDERS, b'\x01')
        burst = [d for f, d in pump(2.5) if f == ECHO and d[:1] == b'B' and len(d) == 3]
        got_n = sorted({d[2] for d in burst})
        fails = (states[-1][7] - before) & 0xff if states else -1
        log.note('burst %d: %d of 16 frames arrived (missing %s); the unit counted %d send failures' % (
            k + 1, len(got_n), ' '.join(str(i) for i in range(16) if i not in got_n) or 'none', fails))
    pump(1.2)
    if states:
        d = states[-1]
        log.note('unit state: GSR %04x TEC %d REC %d lost %d send failures %d' % (d[1] << 8 | d[2], d[3], d[4], d[5] << 8 | d[6], d[7]))
    bus.close()
    log.note('bench done')
    return 0


def decode(argv):
    for line in open(argv[0]):
        parts = line.split()
        if len(parts) >= 4 and parts[1] in ('rx', 'tx') and ';' not in line:
            try:
                d = bytes.fromhex(''.join(parts[4:]))
                what = describe(int(parts[2], 16), d)
                if what:
                    line = line.rstrip('\n') + '  ; ' + what + '\n'
            except ValueError:
                pass
        sys.stdout.write(line)
    return 0


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    cmd, rest = sys.argv[1], sys.argv[2:]
    if cmd == 'run' and len(rest) >= 2:
        return run(rest)
    if cmd == 'bench' and len(rest) >= 2:
        return bench(rest)
    if cmd == 'decode':
        return decode(rest)
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main() or 0)
