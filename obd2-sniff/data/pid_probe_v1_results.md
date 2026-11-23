# pid_probe_C6 v1 results — 2017 Ram HD 6.7 Cummins (CAN-C 500k, OBD pins 6/14)

Source: serial output, 4 runs, 2026-11-22. Read-only Mode 01, requests 0x7DF then 0x7E0, replies from 0x7E8 (ECM).
Run 1 was right after power-up: the first PIDs (00/20/40/60/04/43) timed out while the bus/ECM woke up. Runs 2–4 answered normally.
Run 3 = engine idling; runs 1, 2, 4 = key on, engine off.

## Support bitmaps (ECM 0x7E8)
| Bitmap | Raw | Supported PIDs |
|---|---|---|
| 00 | 41 00 98 1B 00 13 | 01 04 05 0C 0D 0F 10 1C 1F 20 |
| 20 | 41 20 80 03 A0 01 | 21 2F 30 31 33 40 |
| 40 | 41 40 C4 CC 80 4D | 41 42 46 49 4A 4D 4E 51 5A 5D 5E 60 |
| 60 | 41 60 F8 B9 BB 43 | 61 62 63 64 65 69 6B 6C 6D 70 71 73 74 75 77 78 7A 7F 80 |

80 is flagged, so a 81–A0 bitmap exists (v1 did not query it; v2 does).

## Values
| PID | Item | Engine off | Idle (run 3) |
|---|---|---|---|
| 04 | Calculated load (A*100/255) | 0.0 % | 2.4 % (raw 06) |
| 61 | Driver's demand torque (A-125) | 0 % | 0 % |
| 62 | Actual engine torque (A-125) | 0 % | 9 % (raw 86) |
| 6D | Fuel pressure control, support byte 0x03 (cmd + actual FRP A only) | cmd 32000 kPa (4641 psi, parked default); actual 300–450 kPa (44–65 psi, lift pump) | raw 41 6D 03 0C D1 0C A9 → cmd 32810 kPa (4759 psi), actual 32410 kPa (4701 psi) |

- 6D scale: (256*B+C)*10 kPa commanded, (256*D+E)*10 kPa actual (SAE J1979).
- Rail temp: v1 printed -40 °C, but support byte 0x03 has bit 2 clear, so rail temp is **not available** (byte is 00 → -40). Ignore it.
- 22, 23, 43: never answered (not in the support bitmaps) → unsupported. 6D is the only FRP source.
- Load cross-check: PID 04 at idle (2.4 %) is close to broadcast CAN-C 0x101 b2 (~2 % at idle) — educated guess until compared while driving.

Next: pid_probe_C6 v2 (firmware/arduino_ide/pid_probe_XIAO_C6_v2.zip) sweeps every supported PID x3 with decodes.

## v2 full sweep (Nov 22, 2026, engine idling with revs)
- ECUs: 0x7E8 ECM (60 PIDs), 0x7E9 TCM (12 PIDs). The TCM reports MIL ON with 1 DTC and a stuck 152 km/h speed; the ECM shows MIL off with 1 DTC.
- 0x6D FRP tracks: 4,772/4,757 psi at idle, 7,530/7,855 and 9,807/9,848 psi on revs.
- 0x73 exhaust pressure is live: 1.15 to 3.63 to 8.49 psi gauge with revs (decode marked check).
- 0x87 MAP: 0.34 to 2.95 psi gauge. The 0x70 decode is suspect (shows below baro); its raw value tracks MAP as gauge kPa.
- 0x74 turbo rpm 14.8k to 46.4k (check); 0x71 VGT live; 0x77 CACT 36-38 C; 0x0F IAT 62-69 C.
- 0x78/0x98 EGT: all 5 sensors fixed at 150 C (0x076C) across sweeps, which looks like a placeholder, not live.
- 0x7F engine hours 7,310 total, 1,600 idle. 0x31 distance since codes cleared 40,304 mi.
- NOx, DEF, DPF and EGR read 0 at idle.
