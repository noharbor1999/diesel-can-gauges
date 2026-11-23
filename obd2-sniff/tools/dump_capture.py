#!/usr/bin/env python3
"""dump_capture.py - save a node's PSRAM recording as a SavvyCAN CSV file.
Plug the NODE (not the gateway) into the laptop. Close SavvyCAN and the serial monitor.
  pip install pyserial
  python dump_capture.py COM7 idle_headlights.csv
Then in SavvyCAN: File > Load Log File > pick the file (GVRET native CSV).
"""
import sys, time, serial
if len(sys.argv) < 3: print(__doc__); sys.exit(1)
s = serial.Serial(sys.argv[1], 1000000, timeout=2)
time.sleep(0.3); s.reset_input_buffer()
s.write(b"d\n")
n = 0; started = False
with open(sys.argv[2], "w", newline="\n") as f:
    while True:
        line = s.readline().decode(errors="replace").strip()
        if not line:
            if started: break
            continue
        if line.startswith("Time Stamp,"): started = True; f.write(line + "\n"); continue
        if line.startswith("# end of dump"): break
        if started and line[0].isdigit(): f.write(line + "\n"); n += 1
print(f"saved {n} frames to {sys.argv[2]}")
