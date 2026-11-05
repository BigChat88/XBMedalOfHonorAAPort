#!/usr/bin/env python3
"""Sampling profiler for the game running in xemu, through its GDB stub.

Start xemu with -s (GDB stub on port 1234). This stops the guest many times,
reads EIP and resumes it; each sample goes to the function the map file says
owns the address. Static functions are not in the map, so their samples count
for the public symbol placed before them in the same object.

Usage: python tools/gdb-sampler.py [--map build/main.map] [--seconds 60] [--top 40]
                                   [--callers REGEX] [--log FILE --start RE --stop RE]

--log/--start/--stop: sample only from the serial log line matching --start
until the one matching --stop, and print a timeline of the log lines matching
--marks with the seconds since the start (to profile a level load).

--callers: for the samples in functions matching REGEX, scans the stack for
return addresses and counts the functions found. A scan, not a real unwind (the
code has no frame pointers), so it also finds stale addresses: read it as "who
tends to be up the stack", not as exact call chains.

--hist: for the samples in functions matching REGEX, prints a histogram of the
addresses in 32 byte bins, to match against a disassembly.
"""
import argparse
import bisect
import collections
import re
import socket
import time


def load_symbols(path):
    line_re = re.compile(r'^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s+(.*)$')
    symbols = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = line_re.match(line)
            if m:
                symbols.append((int(m.group(2), 16), m.group(1), m.group(3).strip()))
    symbols.sort()
    return symbols, [s[0] for s in symbols]


class Stub:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=10)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b''

    def send(self, payload):
        data = payload.encode()
        self.sock.sendall(b'$' + data + b'#' + b'%02x' % (sum(data) & 0xFF))

    def recv_packet(self):
        while True:
            start = self.buf.find(b'$')
            if start >= 0:
                end = self.buf.find(b'#', start)
                if end >= 0 and len(self.buf) >= end + 3:
                    payload = self.buf[start + 1:end]
                    self.buf = self.buf[end + 3:]
                    self.sock.sendall(b'+')
                    return payload.decode('latin1')
            self.buf += self.sock.recv(65536)

    def request(self, payload):
        self.send(payload)
        return self.recv_packet()

    def interrupt(self):
        self.sock.sendall(b'\x03')
        return self.recv_packet()

    def resume(self):
        self.send('c')


class LogWatch:
    def __init__(self, path):
        self.path = path
        self.pos = 0
        self.tail = b''

    def new_lines(self):
        try:
            with open(self.path, 'rb') as f:
                f.seek(self.pos)
                data = f.read()
        except OSError:
            return []
        self.pos += len(data)
        parts = (self.tail + data).split(b'\n')
        self.tail = parts.pop()
        return [x.decode('latin1').strip() for x in parts]

    def wait_for(self, pattern):
        return any(pattern.search(line) for line in self.new_lines())


def word(hexdata, index):
    # registers come as little endian hex, 8 digits each
    return int.from_bytes(bytes.fromhex(hexdata[index * 8:index * 8 + 8]), 'little')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--map', default='build/main.map')
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--top', type=int, default=40)
    parser.add_argument('--interval', type=float, default=0.02)
    parser.add_argument('--port', type=int, default=1234)
    parser.add_argument('--callers', default=None)
    parser.add_argument('--log', default=None)
    parser.add_argument('--start', default=None)
    parser.add_argument('--stop', default=None)
    parser.add_argument('--marks', default=None)
    parser.add_argument('--hist', default=None)
    args = parser.parse_args()

    symbols, keys = load_symbols(args.map)

    log = LogWatch(args.log) if args.log else None
    if log and args.start:
        while not log.wait_for(re.compile(args.start)):
            time.sleep(0.05)
    started = time.time()
    stop_re = re.compile(args.stop) if args.stop else None
    marks_re = re.compile(args.marks) if args.marks else None

    stub = Stub('127.0.0.1', args.port)
    # code range: from the first symbol to the last one of an object in .text
    text_start = keys[0]
    text_end = 0x600000

    # the stub stops the guest when a debugger attaches: it answers this with
    # the stop reason instead of waiting for an interrupt
    stub.request('?')
    stub.resume()

    callers_re = re.compile(args.callers) if args.callers else None
    hist_re = re.compile(args.hist) if args.hist else None
    hist = collections.Counter()
    callers = collections.Counter()
    caller_samples = 0
    funcs = collections.Counter()
    objs = collections.Counter()
    total = 0
    outside = 0
    end = time.time() + args.seconds
    while time.time() < end:
        time.sleep(args.interval)
        if log:
            done = False
            for line in log.new_lines():
                if marks_re and marks_re.search(line):
                    print(f'{time.time() - started:7.1f}s  {line[:100]}', flush=True)
                if stop_re and stop_re.search(line):
                    done = True
            if done:
                break
        stub.interrupt()
        regs = stub.request('g')
        eip = word(regs, 8)
        i = bisect.bisect_right(keys, eip) - 1
        if callers_re and i >= 0 and eip < 0x80000000 and callers_re.search(symbols[i][1]):
            # read the stack while the guest is stopped
            stack = bytes.fromhex(stub.request('m%x,%x' % (word(regs, 4), 1024)))
            found = set()
            for at in range(0, len(stack) - 3, 4):
                ret = int.from_bytes(stack[at:at + 4], 'little')
                if text_start <= ret < text_end:
                    j = bisect.bisect_right(keys, ret) - 1
                    found.add(symbols[j][1])
            caller_samples += 1
            callers.update(found)
        stub.resume()
        total += 1
        i = bisect.bisect_right(keys, eip) - 1
        if eip >= 0x80000000 or i < 0:
            # kernel: waits, interrupts, file I/O
            outside += 1
            funcs['(kernel)'] += 1
            objs['(kernel)'] += 1
            continue
        funcs[symbols[i][1]] += 1
        objs[symbols[i][2]] += 1
        if hist_re and hist_re.search(symbols[i][1]):
            hist[eip & ~31] += 1

    print(f'{time.time() - started:.1f} s, {total} samples, {100.0 * outside / max(total, 1):.1f}% in the kernel')
    print('--- functions')
    for name, n in funcs.most_common(args.top):
        print(f'{100.0 * n / total:6.2f}%  {name}')
    if callers_re:
        print(f'--- up the stack of {args.callers} ({caller_samples} samples)')
        for name, n in callers.most_common(args.top):
            print(f'{100.0 * n / max(caller_samples, 1):6.1f}%  {name}')
    if hist_re:
        print(f'--- addresses of {args.hist} ({sum(hist.values())} samples)')
        for addr, n in sorted(hist.items()):
            print(f'{addr:08x} {100.0 * n / sum(hist.values()):6.1f}%')
    print('--- objects')
    for name, n in objs.most_common(25):
        print(f'{100.0 * n / total:6.2f}%  {name}')


if __name__ == '__main__':
    main()
