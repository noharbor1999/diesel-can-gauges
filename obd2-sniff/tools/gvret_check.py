#!/usr/bin/env python3
"""gvret_check.py - bench check of the gateway WITHOUT SavvyCAN.
Opens the gateway port, enters GVRET binary mode the same way SavvyCAN does,
and prints frames per second per bus for N seconds.

  pip install pyserial
  python gvret_check.py COM7            (Windows)   or   python gvret_check.py /dev/ttyACM0
  python gvret_check.py --selftest      (no hardware: checks the parser)
Close SavvyCAN first (only one program can open the port).
"""
import sys, time, struct, collections

def parse(buf, out):
    """Parse GVRET bytes. Appends (ts, id, ext, bus, data). Returns unconsumed tail."""
    i = 0
    n = len(buf)
    while i < n:
        if buf[i] != 0xF1:
            i += 1; continue
        if i + 1 >= n: break
        cmd = buf[i + 1]
        if cmd == 0x00:
            if i + 11 > n: break
            ts, rid = struct.unpack_from('<II', buf, i + 2)
            lb = buf[i + 10]; dlc = lb & 0x0F; bus = lb >> 4
            end = i + 11 + dlc + 1
            if end > n: break
            out.append((ts, rid & 0x7FFFFFFF, bool(rid >> 31), bus, bytes(buf[i + 11:i + 11 + dlc])))
            i = end
        else:
            lens = {0x01: 6, 0x06: 12, 0x07: 8, 0x09: 4, 0x0C: 3, 0x0D: 17}
            L = lens.get(cmd, 2)
            if i + L > n: break
            if cmd == 0x0C: print(f"device reports {buf[i+2]} buses")
            i += L
    return buf[i:]

def selftest():
    def frame(ts, rid, ext, bus, data):
        if ext: rid |= 1 << 31
        return bytes([0xF1, 0]) + struct.pack('<II', ts, rid) + bytes([len(data) | (bus << 4)]) + data + b'\x00'
    stream = frame(1, 0x123, False, 0, b'\x01\x02') + bytes([0xF1, 0x09, 0xDE, 0xAD]) + \
             frame(2, 0x18FEF100, True, 4, b'\x00' * 8) + frame(3, 0x7FF, False, 5, b'')
    out = []
    tail = parse(bytearray(stream[:15]), out)
    tail = parse(bytearray(tail + stream[15:]), out)
    assert len(out) == 3 and out[1][1] == 0x18FEF100 and out[1][2] and out[1][3] == 4 and out[2][3] == 5, out
    print("selftest OK:", out)

def main():
    if len(sys.argv) < 2: print(__doc__); return
    if sys.argv[1] == '--selftest': selftest(); return
    import serial
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 20
    s = serial.Serial(sys.argv[1], 1000000, timeout=0.05)
    s.write(bytes([0xE7, 0xE7, 0xF1, 0x0C, 0xF1, 0x06, 0xF1, 0x07, 0xF1, 0x09]))
    buf = bytearray(); counts = collections.Counter(); t0 = tlast = time.time(); total = collections.Counter()
    while time.time() - t0 < secs:
        buf += s.read(65536)
        out = []
        buf = parse(buf, out)
        for f in out: counts[f[3]] += 1; total[f[3]] += 1
        if time.time() - tlast >= 1:
            el = time.time() - tlast; tlast = time.time()
            print("  ".join(f"bus{b}: {counts[b]/el:6.0f} fps" for b in sorted(counts)) or "no frames")
            counts.clear()
            s.write(bytes([0xF1, 0x09]))   # keepalive like SavvyCAN
    print("total frames per bus:", dict(total))

if __name__ == '__main__':
    main()
