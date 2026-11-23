# 2017 Ram 3500 OBD2 sniff — first-pass decode

Truck: 2017 Ram 3500, 6.7 Cummins. Listen-only captures with one XIAO ESP32-C6 and one WCMCU-230, logged in SavvyCAN on the laptop.

The two CSVs are **separate sessions** (not simultaneous). Timestamps are seconds from the start of **that** file. Do not line them up against each other.

Re-run the analysis:

```
python3 /workspace/maker/projects/obd2-sniff/decode/decode_logs.py
python3 /workspace/maker/projects/obd2-sniff/decode/run2_decode.py
python3 /workspace/maker/projects/obd2-sniff/decode/drive_decode.py
python3 /workspace/maker/projects/obd2-sniff/decode/dimmer_decode.py
```

## Files

| Path | What it is |
|---|---|
| `../data/logs/raw_942e.csv` | Original fast log (kept) |
| `../data/logs/raw_6d05.csv` | Original slower log (kept) |
| `../data/logs/pins_6-14.csv` | Copy of `raw_942e.csv`, named for the bus we assigned |
| `../data/logs/pins_3-11.csv` | Copy of `raw_6d05.csv`, named for the bus we assigned |
| `decode_logs.py` | This analysis |
| `id_table_pins_6-14.csv` | Every CAN-C ID: count, period, DLC, changing vs constant bytes, function guess |
| `id_table_pins_3-11.csv` | Same for CAN-IHS |
| `events_pins_6-14.csv` | Curated control / powertrain events on CAN-C |
| `events_pins_3-11.csv` | Curated events on CAN-IHS |
| `signals_pins_6-14.csv` | Decoded traces on a 50 ms grid (RPM, throttle, brake, stalk, dimmer) |
| `signals_pins_3-11.csv` | Decoded traces (RPM, battery, ignition, dimmer, stalk) |
| `plots/pins_6-14_rpm_throttle_brake.png` | Best overview of the CAN-C log |
| `plots/pins_6-14_lights_dimmer.png` | Dash illumination and lighting on CAN-C |
| `plots/pins_3-11_rpm_battery_dimmer.png` | Best overview of the CAN-IHS log |

| `../data/logs/raw_60e4_run2.csv` | Original run-2 IHS log (kept) |
| `../data/logs/raw_deb7_run2.csv` | Original run-2 CAN-C log (kept) |
| `../data/logs/pins_3-11_run2.csv` | Copy of `raw_60e4_run2.csv` |
| `../data/logs/pins_6-14_run2.csv` | Copy of `raw_deb7_run2.csv` |
| `run2_decode.py` | Timed-script analysis for run 2 |
| `events_pins_3-11_run2.csv` | Curated run-2 events on CAN-IHS |
| `events_pins_6-14_run2.csv` | Curated run-2 events on CAN-C |
| `plots/run2_dimmer.png` | Run 2 headlights then dimmer step (both buses) |
| `plots/run2_locks_turns.png` | Run 2 locks and turn signals (both buses) |
| `plots/run2_boost.png` | Run 2 CAN-C RPM, throttle, and boost/MAP candidates |

| `../data/logs/raw_bb54_drive.csv` | Original unlabeled drive log (kept) |
| `../data/logs/pins_6-14_drive.csv` | Copy of `raw_bb54_drive.csv` — CAN-C drive (sort timestamps before use) |
| `../data/logs/raw_883f_dimmer.csv` | Original IHS dimmer-sweep log (kept) |
| `../data/logs/pins_3-11_dimmer.csv` | Copy of `raw_883f_dimmer.csv` |
| `../data/logs/raw_38dc_dimmer.csv` | Original CAN-C dimmer-sweep log (kept) |
| `../data/logs/pins_6-14_dimmer.csv` | Copy of `raw_38dc_dimmer.csv` |
| `drive_decode.py` | Drive-log analysis (sorts timestamps, drops 164 out-of-order prefix frames) |
| `dimmer_decode.py` | Dimmer-sweep analysis |
| `events_pins_6-14_drive.csv` | Curated drive events |
| `events_dimmer.csv` | Curated dimmer-sweep events (both buses) |
| `plots/drive_boost.png` | Drive: RPM, throttle, speed, 0x30A byte6 |
| `plots/drive_exhaust_brake.png` | Drive: coasts, 0x30A byte7, gear, Tow/Haul candidate |
| `plots/dimmer_sweep.png` | Dimmer wheel vs illumination output, both buses |

| `../data/logs/raw_e884_markers.csv` | Original IHS marker log (kept) |
| `../data/logs/pins_3-11_markers.csv` | Copy of `raw_e884_markers.csv` — CAN-IHS, 12 markers |
| `../data/logs/raw_153c_markers.csv` | Original CAN-C marker log (kept) |
| `../data/logs/pins_6-14_markers.csv` | Copy of `raw_153c_markers.csv` — CAN-C, markers 15–36 (counter did not reset) |
| `marker_decode.py` | IHS marker-log analysis |
| `canc_marker_decode.py` | CAN-C marker-log analysis |
| `events_pins_3-11_markers.csv` | Curated IHS marker-log events |
| `events_pins_6-14_markers.csv` | Curated CAN-C marker-log events |
| `plots/ihs_markers.png` | IHS log: speed/RPM, APS-like, gear, Tow/Haul, marker lines |
| `plots/canc_markers.png` | CAN-C log: speed/RPM, APS, 0x30A switch + byte 7, Tow/Haul, marker lines |
| `../firmware/arduino_ide/pid_probe_XIAO_C6_v1.zip` | One-shot Mode 01 PID probe (load + fuel rail) for the C6 sniffer after CTX is moved to D3 |
| `markers.py` | List capture-marker frames (extended ID 0x1FFFFFF0) from a SavvyCAN CSV: marker number + seconds from log start |
Plots use blue, orange and gold (not red vs green) and every series is labeled.

## Bus assignment

**High confidence. The file names match the educated guess in the task.**

| File | Frames | Duration | IDs | Rate | Assigned bus |
|---|---:|---:|---:|---:|---|
| `raw_942e.csv` → `pins_6-14.csv` | 248,090 | 138.537 s | 131 | 1790.8 /s | **CAN-C powertrain, 500 kbit/s** |
| `raw_6d05.csv` → `pins_3-11.csv` | 21,103 | 98.372 s | 85 | 214.5 /s | **CAN-IHS interior/body, 125 kbit/s** |

Why this is not just a rate argument:

- The fast log carries the Chrysler/Ram HD powertrain set at the rates in [opendbc `chrysler_ram_hd_generated.dbc`](https://github.com/BMW-E8x-E9x/opendbc/blob/master/opendbc/dbc/chrysler_ram_hd_generated.dbc): 0x108 ECM_1 RPM at 10 ms, 0x140 ESP_1 and 0x11C ESP_8 at ~20 ms, 0x102 steering at 10 ms, 0x120 ECM_2 (DLC 7) at 20 ms, 0x23A cruise buttons (DLC 3) at 20 ms, 0x158 wheel speeds at 20 ms. It does **not** have IHS 0x322.
- The slower log has the FCA interior set documented by [magikh0e's FCA message reference](https://magikh0e.pl/pubCarHacking/bus-message-reference.html) and the [JL Wrangler community map](https://www.jlwranglerforums.com/forum/attachments/jl_wrangler_can_bus_reference_v2-pdf.1076091/): 0x322 RPM/speed at 100 ms, 0x2C2 battery (DLC 4) at 200 ms, 0x122 ignition at 100 ms, 0x1C0 RKE at 200 ms, 0x2D3 NM-wake, 0x2FA / 0x3B2 body lighting. It does **not** have 0x108 / 0x140 / 0x11C.
- OBD pin assignment matches public FCA practice: pins 6/14 = diagnostic CAN-C, pins 3/11 = diagnostic CAN-IHS ([Open Labor Project](https://openlaborproject.com/diagnostics/can-bus/ram/), [MACS March 2020 service report](https://www.macsw.org/common/Uploaded%20files/MACS%20Service%20Reports/2020_03msr4email.pdf), magikh0e DLC table). One 2009–2017 draft left the IHS DLC pins unresolved; **this truck confirms live IHS traffic on 3/11**.
- Same 11-bit ID can appear on both buses with a **different DLC and meaning**. Do not apply the IHS 0x122 or 0x2C2 decode to the CAN-C copies (CAN-C 0x122 is DLC 6 and almost constant; CAN-C 0x2C2 is DLC 8 and static).

The [Colorado State TUCRRC older-Ram ID list](https://www.engr.colostate.edu/~jdaily/tucrrc/DodgeCAN.html) does **not** match this 2017 log and was not used for decodes.

All payloads on both logs are standard 11-bit, direction Rx, SavvyCAN bus 0. Timestamps in the CSV are microseconds.

## Important platform caveat

Most public FCA byte maps were captured on Jeep JL / Gladiator / later Stellantis, or on 2019+ Ram HD (the commaai DBC). This truck is a 2017 4th-gen Ram 3500. Powertrain IDs 0x108 / 0x11C / 0x120 / 0x140 / 0x23A / 0x318 **matched on this log**. Body/IHS content is more likely to differ. Jeep 0x291 (the usual headlight / dimmer frame) is **absent from both buses**. Every finding below is tagged **known (source)** or **educated guess (basis)**. Nothing was invented to fill a gap.

## Dash dimmer / headlight illumination (gauge project)

Jeep-style 0x291 was not on either bus. The best candidates on **this** truck, both using the documented Jeep dimmer maximum **0xC8 (200)**:

| Bus | ID | Byte (0-based) | Off | On (this log) | Period | Confidence |
|---|---|---|---:|---:|---:|---|
| CAN-C (pins 6/14) | **0x334** (BCM_1) | **byte 6** | 0x00 | 0xC8 | 1.00 s | educated guess |
| CAN-IHS (pins 3/11) | **0x2FA** | **byte 2** | 0x00 | 0xC8 | 0.50 s | educated guess |

Companion flags that moved at the same time:

- CAN-C 0x334 byte 7 bit 2 went high during high beam (92.17–94.33 s), which matches opendbc `HIGH_BEAM_DISPLAY` and 0x318 stalk byte 0 = 0x04. **known ID, verified by coincidence.**
- CAN-IHS 0x3B2 byte 0 = 0x03 idle, 0x83 while lights were on (59.49–61.70 s), then 0x13 / 0x0B during the two turn-signal windows. **educated guess.**

**What is not proven yet:** both dimmer bytes only took the values 0x00 and 0xC8. That is enough to say "interior illumination is on, at the last/max level," but it is **not** enough to say the byte tracks the dimmer wheel through intermediate steps (Jeep 0x291 is documented as 0x22–0xC8). Next capture: turn the lights on, then **sweep the dash dimmer wheel slowly** from min to max, and save a new IHS (and/or CAN-C) log.

For the A-pillar gauges, either bus can be used. IHS 0x2FA byte 2 is on the body bus and updates twice a second; CAN-C 0x334 byte 6 is the BCM copy at 1 Hz.

## Top findings

### Continuous / analog (known, verified on these logs)

- **Engine RPM, CAN-C 0x108 bytes 0–1 big-endian, 1 rpm/bit.** known (opendbc ECM_1). pins_6-14: 0 until 63.75 s, crank to 942, idle ~700, later blips to 2664.
- **Engine RPM, CAN-IHS 0x322 bytes 0–1 big-endian; 0xFFFF (and 0) = engine off.** known (magikh0e / JL). pins_3-11: off until 36.90 s, idle ~700, later blip to 2411. Speed bytes 2–3 stayed 0 (truck was parked); the published `/200 = mph` formula was not exercised.
- **Vehicle speed, CAN-C 0x11C bytes 4–5 * 0.0078125 km/h.** known (opendbc ESP_8). Stayed 0.0 km/h the whole log.
- **Accelerator, CAN-C 0x120 byte 2 * 0.4 %.** known (opendbc ECM_2). Zero at idle; 24 % then 66 % in the two RPM blips.
- **Battery voltage, CAN-IHS 0x2C2 byte 2 / 10 V.** known (magikh0e $2C2). 12.5–12.7 V key-on, sag to 10.6 V at crank, 13.7–14.5 V with the engine running.

### Discrete controls (mix of known and educated guess)

- **Brake pedal, CAN-C:** 0x11C BRAKE_PEDAL raw and 0x140 Brake_Pedal_State (byte 0 0x80 idle, 0x85 pressed) both move at 108.70–112.90 s. **known (opendbc).**
- **Brake pedal, CAN-IHS:** no high-confidence match. 0x332 byte 2 pulsed 0x18↔0x58 at 78.06–82.75 s; JL calls 0x332 a brake/gear ID. **educated guess, unconfirmed.**
- **High beam:** CAN-C 0x318 byte 0 = 0x04 and 0x334 byte 7 bit 2, 92.17–94.33 s, plus a short flash at 126.64–127.52 s. **known IDs, verified.**
- **Turn signals:** 0x318 byte 0 = 0x01 or 0x02, with blinking lamp outputs on CAN-C 0x334 byte 3 (0x40 / 0x80) and IHS 0x3B2 byte 0 (0x0B / 0x13). **known as turn signals.** Left vs right is **unconfirmed** (JL says 0x01=left, 0x02=right; 5th-gen BCM bit names point the other way).
- **Wipers:** 0x318 byte 2 stepped 0→1→2→3→4→5→0x0D→0x0E and back down, once on each log. **educated guess** (JL puts wipers in 0x318; the burst looks like a stalk sweep).
- **Ignition, CAN-IHS 0x122:** key RUN at 6.10 s (byte 0 = 0x04, engine still off), crank codes 0x05/0x15 at 36.12–37.00 s, then 0x04 with the engine running. **known ID; byte values are an educated guess from RPM.** Jeep's published codes do not map one-for-one (Jeep lists 0x0402 as "engine running").
- **Locks:** not clearly captured. 0x1C0 stayed at the idle pattern `00 00 00 80 11 48`. Jeep lock command codes (byte 0 = 0x21/0x23/0x24) never appeared. A 0x1C0 byte-4 change 0x11→0x51 at 6.12–9.13 s on IHS lines up with key RUN, not a lock press.
- **0x318 byte 0 = 0x40** short pulse (CAN-C 124.08–124.40 s, IHS 62.84–63.08 s): unknown column/stalk button. **unknown.**
- **Coolant temperature:** no high-confidence decode. A few slowly drifting bytes exist (for example CAN-C 0x180 byte 1 at 123–125, which would be 83–85 °C if the common `byte-40` rule applied to an already-warm engine). **not claimed.**

## Event times to confirm

Times are seconds from the start of that log. The two captures used the same kind of routine (key on, start, idle, then controls) but **not the same clock**.

### pins_6-14.csv (CAN-C), 138.5 s

| t start | t end | What we think happened |
|---:|---:|---|
| 0.00 | 63.75 | Key on, engine off (RPM = 0) |
| 63.75 | 64.50 | Engine crank / start (RPM to 942, then ~700 idle) |
| 90.01 | 95.42 | Lights on, then off. 0x334 byte 6 = 0xC8 while on |
| 92.17 | 94.33 | High beam during that lights-on window |
| 97.04 | 105.92 | Wipers through the speeds, then off |
| 108.70 | 112.90 | Brake pedal pressed and released |
| 115.12 | 115.76 | Light throttle (~24 %) |
| 117.82 | 118.58 | Larger throttle blip (66 %, RPM to 2664) |
| 124.08 | 124.40 | Unknown 0x318 = 0x40 pulse |
| 126.64 | 127.52 | Short high-beam flash |
| 128.24 | 130.56 | Turn signal A (stalk 0x01) |
| 131.60 | 134.64 | Turn signal B (stalk 0x02) |

### pins_3-11.csv (CAN-IHS), 98.4 s

| t start | t end | What we think happened |
|---:|---:|---|
| 0.00 | 6.10 | Key activity, then key to RUN at 6.10 s (engine still off) |
| 36.12 | 37.00 | Crank / start (RPM leaves 0 at 36.90 s; battery sags to 10.6 V) |
| 37.00 | 59.00 | Idle, alternator charging (~13.8 V) |
| 59.49 | 61.70 | Lights on ~2.2 s, then off (0x2FA byte 2 = 0xC8) |
| 62.84 | 63.08 | Unknown 0x318 = 0x40 pulse |
| 64.12 | 71.00 | Wipers on, through the speeds, then off |
| 72.52 | 73.80 | Turn signal (stalk 0x02) |
| 74.52 | 76.84 | The other turn signal (stalk 0x01) |
| 78.06 | 82.75 | Possible brake pulses on 0x332 (unconfirmed) |
| 85.00 | 90.50 | Throttle blip (RPM to 2411) |

If this order is wrong, say which event is mis-labeled and we will retag the bytes.

## Suggested next capture (one at a time, IHS first if you only do one)

1. Lights on, then **sweep the dimmer wheel** min → max → min. That is the test that proves or kills the 0x2FA / 0x334 byte-6 level theory.
2. Lock and unlock from the driver door switch, then from the fob, each as its own short log (or with a spoken/note timestamp). 0x1C0 did not move like a lock command this time.
3. Hold the brake pedal once, steadily, for a few seconds, so the IHS 0x332 guess can be accepted or dropped.
4. Name left vs right turn signal in the file name or a note.

## Sources

- commaai / community opendbc `chrysler_ram_hd_generated.dbc` (Ram HD powertrain and BCM IDs)
- [magikh0e FCA CAN Bus & Message Reference](https://magikh0e.pl/pubCarHacking/bus-message-reference.html) — mostly Jeep; used for 0x322, 0x2C2, 0x122, 0x1C0, 0x2D3, 0x291 (absent here)
- [JL Wrangler CAN Bus Reverse notes](https://www.jlwranglerforums.com/forum/attachments/jl_wrangler_can_bus_reference_v2-pdf.1076091/) — 0x318 wipers, 0x2FA / 0x3B2 lighting status, 0x332 brake/gear
- [CarGeek engine-speed / vehicle-speed pages](https://cargeek.org/can/signals/engine-speed) — Chrysler 0x108 / 0x140 / 0x11C
- [Open Labor Project, Ram CAN architecture](https://openlaborproject.com/diagnostics/can-bus/ram/) and [MACS March 2020](https://www.macsw.org/common/Uploaded%20files/MACS%20Service%20Reports/2020_03msr4email.pdf) — DLC pin / bus-speed assignment

---

## Run 2 — timed script (2026-11-22)

Each bus was re-run following a fixed ~10 s/step phone-stopwatch script. The two CSVs are still **separate sessions**. Do not line the clocks up. Timestamps below are seconds from the start of **that** file.

Re-run: `python3 /workspace/maker/projects/obd2-sniff/decode/run2_decode.py`

### File-to-bus mapping

**Confirmed.** Same ID sets as run 1.

| File | Size / frames | Duration | IDs | Rate | Assigned bus |
|---|---|---:|---:|---:|---|
| attachment `60e46777…`.csv → `raw_60e4_run2.csv` → `pins_3-11_run2.csv` | 2.4 MB / 44,491 | 217.314 s | 84 | 204.7 /s | **CAN-IHS, pins 3/11, 125 kbit/s** |
| attachment `deb792699…`.csv → `raw_deb7_run2.csv` → `pins_6-14_run2.csv` | 21.2 MB / 384,930 | 235.604 s | 131 | 1633.8 /s | **CAN-C, pins 6/14, 500 kbit/s** |

The CAN-C average rate is lower than run 1 because of the key-off period and a complete silence gap from 10.00–15.00 s (0 frames). 0x108 first appears at 16.29 s. IHS never went fully silent.

Script (as run): (1) key off 10 s; (2) key ON engine off 20 s; (3) lock 10 s, unlock 10 s; (4) headlights on 10 s, off 10 s; (5) dimmer wheel all the way down 10 s, all the way up 10 s; (6) wipers low 10 s, off 10 s; (7) brake 5 s, release 10 s; (8) left turn 5 s off, right turn 5 s off; (9) start, idle 30 s; (10) rev ~2000 rpm twice, 3 s each, 5 s apart; (11) key off 10 s.

Anchors used to map the later steps: IHS key RUN at 13.19 s (`0x122` 04 02), engine start RPM>200 at 143.06 s, key off 199.75 s. CAN-C key ON at 16.29 s, start 155.12 s, 0x108 stops at 217.53 s still showing ~695 rpm (ECM stopped broadcasting; it did not send RPM=0 first).

Held times were close to the script. Revs were a little longer than 3 s, and the gap between them was about 7 s (IHS) / 7 s (CAN-C) instead of 5 s. On the CAN-C pass, the right turn was not logged (only left appeared).

### Per-step findings

| Step | IHS (`pins_3-11_run2.csv`) | CAN-C (`pins_6-14_run2.csv`) | Label |
|---|---|---|---|
| 1–2 Key off → key ON | Residual `0x122` 00 01 from t≈0. 12.55 00 02, 12.73 03 02, **13.19 04 02** = key RUN, engine off. | Traffic 0–10 s, **silence 10.00–15.00 s**, 0x108 appears at **16.29 s**, RPM=0. | confirmed by script |
| 3 Lock / unlock | **0x2FA byte 0** 00→**0x40** at 26.05, back to 00 at 38.07 (~12 s locked). Byte 3 0x18→00 at lock, 00→08→18 at unlock. **0x2E2 byte 0** pulse 00→04 at 26.01 (lock), 00→02 at 38.04 (unlock). **0x1C0 did not carry a lock command** (only b4 11→51 at key RUN). | **0x334 byte 0** 0x40→**0x00** at 29.53, back to 0x40 at 42.88 (~13 s). Polarity is **opposite** IHS 0x2FA byte 0 (0x40 = locked on IHS, 0x00 = locked on CAN-C). 0x1C0 again only tracks key RUN. | confirmed by script |
| 4 Headlights on / off | **49.36–59.43**: **0x2FA byte 2 = 0xC8**, **0x3B2 byte 0 = 0x83**. Then both return to off. | **55.82–68.24**: **0x334 byte 6 = 0xC8**, byte 7 = 0x02, byte 2 = 0x01. | confirmed by script |
| 5 Dimmer wheel min / max | After lights off, **0x2FA byte 2 stayed 0x00**. Bytes 4 and 6 went 00→0xC8→00 in **1.2 s** (79.59–80.79). No intermediate levels, not a 10+10 s sweep. 0x214 wander is not dimmer. | After 68.24, **0x334 byte 6 stayed 0x00** through the whole dimmer window. No other 0–200 proportional candidate besides noise. | confirmed by script (no proportional level recorded; see verdict) |
| 6 Wipers low / off | **89.65–101.17**: **0x318 byte 2** stepped then held **0x0D** ~11 s, then off. | **103.71–114.35**: 0x318 byte 2 held at 0x0D; 0x334 byte 7 = 0x01 while wiping. | confirmed by script (0x0D = low/continuous on this truck) |
| 7 Brake 5 s / release | **0x332 byte 2** 0x18→**0x58** at 111.81, back 0x18 at 117.75 (~6 s). (0x322 byte 4 0x34→0x35 in the same window — possible coincidence, not claimed.) | **0x11C** pedal raw 126.56–132.38 (max 1199); **0x140 byte 0** 0x80→0x85 at 126.58, back 0x80 at 132.68. | confirmed by script; CAN-C brake known (opendbc). IHS 0x332 was a run-1 guess, now confirmed. |
| 8 Left then right turn | **Left 128.45–133.87**: 0x318 byte 0 = **0x01**, 0x3B2 byte 0 blinks **0x0B**. **Right 135.25–140.37**: 0x318 byte 0 = **0x02**, 0x3B2 byte 0 blinks **0x13**. | **Left only, 143.15–149.47**: 0x318 byte 0 = 0x01, 0x334 byte 3 blinks **0x40**. No 0x02 / 0x80 in this file — right turn was missed or skipped on the CAN-C pass. Combined with IHS, lamp 0x80 is the right-side bit (seen in run 1). | confirmed by script on IHS. Resolves run-1 left/right: **0x01 = left, 0x02 = right** (matches the JL Wrangler map on this truck). |
| 9 Start / idle | 0x122 05 then 15 at 142.21–142.29; RPM>200 at **143.06**; idle ~700 for ~27 s. | Last RPM 0 at 154.69; first >200 at **155.12**; crank peak then idle ~700 for ~34 s. | confirmed by script; RPM known (opendbc / IHS 0x322). 0x122 crank codes remain an educated guess vs Jeep maps. |
| 10 Two ~2000 rpm revs | First ~170–180 s, peak 2154. Second ~185–195 s, peak 2131. | Rev 1: APS 189.45–195.59 (max 11.6%), RPM peak ~2040–2052. Rev 2: APS 202.65–210.51 (max 11.2%), RPM peak 2131. | confirmed by script; throttle known (opendbc 0x120 byte 2 × 0.4%) |
| 11 Key off | RPM 0 at 199.95; 0x122 → 03 at 199.75, 00 01 at 200.43. | 0x108 last frame 217.53 still ~695 rpm, then traffic decays. | confirmed by script |

Full event rows (observed bytes + confidence) are in `events_pins_3-11_run2.csv` and `events_pins_6-14_run2.csv`.

### Dimmer verdict

**0x334 byte 6 (CAN-C) and 0x2FA byte 2 (IHS) are illumination-on outputs**, not a live dimmer-wheel position. They only go to **0xC8 while headlights / park lights are on**, and stay **0x00 when lights are off**. That is now confirmed by script (step 4 vs step 5).

Step 5 was run with the headlights off, so those illumination bytes never moved. No other ID showed a 10 s + 10 s proportional min→max sweep. The only dimmer-window activity was IHS **0x2FA bytes 4 and 6** jumping 0→0xC8→0 for **~1.2 s** at 79.59–80.79. That is a real event in the right window, but it is not a linear level and is not proven as the wheel.

**Next capture (required to get a usable gauge dimmer signal):** lights **ON**, then sweep the dimmer wheel **slowly** min → max → min. Log IHS (and CAN-C if convenient). Until that exists, the A-pillar gauges can only use the 0 / 0xC8 illumination-on flag.

### (e) Coolant temp

**No high-confidence decode. Not claimed.** The engine already looked warm (prior run, then this one). Slowly drifting bytes exist, all **educated guess** only, not to be used yet:

- CAN-C **0x3C0 byte 0** 107→115 (would be 67–75 °C if the common byte−40 scale were right).
- CAN-C **0x31F byte 1** 92→96 (52–56 °C if byte−40) — more IAT-like than coolant, still unproven.
- Other slow wanderers: 0x200 byte 6, 0x27F byte 1, 0x214 byte 0. IHS **0x2C2 byte 2** rise is **battery voltage**, not coolant (known, magikh0e).

EGT was not identified.

### (f) Turbo boost / MAP

Public 2013–2018 6.7 Cummins boost in community tools is **requested**, not a documented broadcast: Torque Pro PID 0x170 / 0x187, ScanGauge UDS `22 81 1C` (DID 0x811C). No public broadcast ID was found for this truck.

**Best broadcast candidate, educated guess (correlation with the two no-load revs):** CAN-C **0x30A byte 6**, 100 ms. Raw 0 at key-on and idle. Rises on both revs **with lag behind APS/RPM**, then decays after throttle drops:

- Rev 1: still 0 at t=189 (APS already up), 15 at 191, 20 at 193, **26 at 195**; 3 at 197 after APS ended 195.59. Phase peak 27.
- Rev 2: 0 at 203, 3 at 206, **28 at 209**, 17 at 211 after APS ended 210.51. Phase peak 31.

Scale is **not claimed**. If the unit were kPa gauge, a peak of ~27–31 kPa (~4 psi) would fit a no-load blip. If it were psi, ~28 psi unloaded would be surprisingly high. Do not invent a scale. Confirm later with a known boost gauge, a loaded pull, or a UDS DID 0x811C capture.

Ignore **0x30A byte 4**: it toggles 2↔194 every 200 ms (1006 changes) — a counter / alive pattern, not a flag.

Other risers, treat carefully (all educated guess):

- **0x2A0 byte 2** idle 58, revs 91–92: jumps with APS more tightly than 0x30A byte 6 (possible load or VGT-related; 58 is low for a raw kPa atmosphere unless scaled).
- 0x2FC byte 5, 0x134 byte 5, 0x101 byte 3 track APS too tightly to be boost (likely throttle/load copies).
- 0x144 bytes 5–6 track RPM (another RPM-like signal).
- 0x108 torque is known in opendbc but is not boost; the 13-bit 0.25 / −500 decode went negative in places during revs and was not used here.

See `plots/run2_boost.png`.

### (g) Exhaust brake (6.7 Cummins VGT)

**Not toggled in this script. No confirmed on/off/auto switch or active flag.**

What was looked at and rejected or left open:

- **0x30A byte 0** 255→0 at 17.21 s (just after key on) and stays 0 — looks like a key-on/init value, not an exhaust-brake switch.
- **0x30A byte 4** is the 200 ms toggle described above, not a status flag.
- **0x2A0 byte 2** moving 58→92 with the revs could be VGT-vane or load related, but vanes also move for ordinary boost control. It cannot be claimed as exhaust-brake state.

A **dedicated capture is required**: key on and engine running, toggle the exhaust-brake control **off / on / auto** (and, if it has a separate “active while braking / coasting” condition, include that). Until that log exists, do not decode a switch or an active flag. A VGT position byte, if it is 0x2A0 byte 2 or something else, can only be proven in that same capture or against a known vane/boost display.

### Plots

| Path | What it shows |
|---|---|
| `plots/run2_dimmer.png` | Best dimmer plot: IHS 0x2FA / 0x3B2 and CAN-C 0x334 during headlights, then the dimmer window (illumination stays 0; only a 1.2 s 0xC8 pulse on 0x2FA bytes 4/6). |
| `plots/run2_locks_turns.png` | Locks (IHS 0x2FA / 0x2E2, CAN-C 0x334 byte 0) and turns (left vs right on IHS; left only on CAN-C). |
| `plots/run2_boost.png` | Extra: CAN-C RPM, throttle, 0x30A byte 6 lagging the two revs, and 0x2A0 byte 2. |

### Suggested next capture (updated after run 2)

1. **Lights ON**, then **slow** dimmer-wheel sweep min → max → min. This is still the missing gauge-dimmer test.
2. Exhaust brake: key on, engine running, toggle **off / on / auto** (and an active coast/brake if you can do it safely). Needed for priority (g).
3. Optional: a short loaded pull or a UDS DID 0x811C / PID 0x170 request alongside 0x30A, to scale or kill the boost candidate. Coolant/IAT/EGT still need a from-cold start or a known PID request.

Lock, left vs right, wipers, headlights, and the IHS brake flag are settled enough that they do not need a repeat unless you want fob-vs-switch on 0x1C0.

---

## Drive log — pins 6/14 CAN-C (2026-11-22)

Unlabeled attachment `bb54e5f76ee5…`.csv (21.3 MB). **Confirmed CAN-C, pins 6/14, and a real drive** (not a dimmer sweep). Same ID set as the earlier CAN-C logs: 0x108 / 0x11C / 0x120 / 0x140 present, 0x322 / 0x2FA absent, 0x122 DLC 6.

**Timestamp caveat (important):** the CSV is **not in time order**. The first 164 rows belong at t ≈ 77.28 s and were prepended ahead of the start of the capture. After sorting by `Time Stamp`, duration is **220.486 s** (388,211 frames, 127 IDs, 1760.7 /s). All times below are seconds from the earliest timestamp. `drive_decode.py` drops those 164 prefix rows (they interleaved and caused brief flag chatter) and sorts the rest; t=0 is unchanged. Raw copies are kept.

Note: exhaust-brake switch was **ON before logging** and was **not cycled at idle**. Partway through he turned **Tow/Haul ON** (holds the torque converter locked for stronger exhaust braking). Pattern: boost pull, then coast with throttle released and service brake not used, repeated.

### Duration and speed

| | |
|---|---|
| Log duration (sorted) | 220.486 s |
| Moving (speed > 1 km/h) | 2.95–205.35 s (202.4 s span) |
| Speed range | **0–107.5 km/h (0–66.8 mph)** |
| RPM | 631–2773 |
| APS | 0–83.7 % |
| Ends | stopped, engine idling, key still on (~205–220 s) |

### (1) Segmented events

Times in `events_pins_6-14_drive.csv`. Hard-throttle pulls (APS ≥ 60 % for >1 s) and no-pedal coasts (APS < 3, speed > 15 km/h, service-brake pedal raw < 30):

| Kind | t start–end | What happened | 0x30A b6 peak / notes |
|---|---|---|---|
| Boost 1 | 24.90–31.90 (7.1 s) | APS ~74 %, RPM ~2600, speed 6→86 km/h | **204** at 26.51 |
| Coast 1 | 32.00–51.15 (19.2 s) | 89→44 km/h, 2.37 km/h/s; Tow/Haul candidate at 34.64 | b7 on after boost bleeds |
| Boost 2 | 51.80–58.60 (6.9 s) | APS ~80 %, hardest pull | **242** at 55.41 (highest) |
| Coast 2 | 58.70–77.30 (18.6 s) | 101→44 km/h, **3.11 km/h/s**; locked-ratio downshifts | b7 med ~46 |
| Boost 3 | 97.50–99.60 (2.1 s) | shorter pull | **179** at 98.81 |
| Coast 3 | 99.70–104.40 (4.7 s) | 51→47 km/h, only 1.01 km/h/s | b7 = 0 (weak / not engaged) |
| (stop) | 104.5–117 | service brake to a stop, idle | |
| Boost 4 | 132.20–143.00 (10.8 s) | longest pull, peak speed 107.5 km/h | **205** at 139.41 |
| Coast 4 | 143.15–159.10 (16.0 s) | 107→67 km/h, 2.52 km/h/s | b7 on between downshifts |
| Boost 5 | 161.40–164.50 | short pull | **187** at 162.52 |
| Coast 5 | 164.45–168.50 (4.0 s) | 95→85 km/h, 2.49 km/h/s | b7 on |
| Coast 6 | 171.15–179.80 (8.7 s) | 67→35 km/h, **3.77 km/h/s**; RPM to 2510, down to 3rd | b7 max **81** |

Service-brake applications (0x11C pedal / 0x140, **known, opendbc**) are separate: 15.4–19.4, 79–80, 86–90, 104–112, 122–128, 168.6–171.1, 194–205.

### (2) Boost / MAP verdict

**Best candidate still CAN-C 0x30A byte 6** (100 ms). Idle / stopped = 0; rises with load and **lags APS**, then decays after lift. Peaks 204 / **242** / 179 / 205 / 187 on the five pulls.

**Scale, educated guess (not sourced):** **1 count ≈ 1 kPa gauge** (≈ 0.145 psi). The hardest pull (242) is **35.1 psi**, at the top of a typical stock 6.7 Cummins 25–35 psi full-load band. The other pulls land at 26–30 psi. This also fits the no-load run-2 blips of 27–31 (≈ 4 psi). It is **not** raw psi (242 psi is impossible) and **not** kPa absolute (idle is 0, not ~101). Confirm later against a known boost gauge or UDS DID 0x811C.

0x2A0 byte 2 (idle ~53, boost 90–95, also high on coast) tracks load / possible VGT, **not** a clean MAP, and is not used for the scale. Public 2013–2018 6.7 boost remains a requested PID/DID, not a documented broadcast.

### (3) Exhaust brake, Tow/Haul, gear / TCC

**Switch state:** not cycled in this log (ON before logging, never flipped at idle). No off/on/auto decode is claimed. A dedicated idle toggle is still required to isolate a switch byte. Comparing constants to run 2 is too confounded (motion, temps, Tow/Haul, gear).

**Active / effort candidate, educated guess:** **0x30A byte 7**. Analog 35–81, **0 during boost and when stopped**, non-zero during many no-pedal coasts after boost has decayed; drops during downshifts. Not a clean binary flag. May be exhaust backpressure or engine-braking effort. Coast 3 (b7 = 0, weak decel) looks like the brake was not actually applying.

**VGT position:** no dedicated vane byte proven. 0x2A0 byte 2 moves with load and stays mid-high on coasts, but vanes also move for ordinary boost control.

**Tow/Haul, educated guess:** **0x170 byte 1 bit 1** goes **252 → 254 at 34.64 s** (during coast 1) and **stays 254** through the end of the log. The same bit was **0 (byte 252)** for the entire engine-running portion of run 2 (Tow/Haul not used). After 34.64 s, coasts show tight TCC-locked RPM/speed plateaus and automatic downshifts; coast 2 decel (3.11 km/h/s) is stronger than coast 1 overall (2.37). The first 2.6 s of coast 1 is not a fair baseline (boost still decaying). Weaker alternate, not preferred: 0x27F byte 1 45→46 at 84.04 s (coast 2 was already stronger before that; could be a 1-count temp).

**Gear, educated guess:** **0x2A8 byte 2** in steps of 17: **17 / 34 / 51 / 68 / 85 / 102 ≈ 1st–6th** (0 near stop / P-N). Changes line up with RPM jumps on the coasts. Locked-TCC RPM/km/h plateaus: **≈16.98 (6th), 21.96 (5th), 29.5 (4th), 44.04 (3rd)**. TCC lock itself is visible as those rock-stable ratios during closed-throttle coasts after Tow/Haul; no isolated TCC bit is claimed.

### (4) Temp candidates (educated guess, not claimed as known)

Engine was already warm.

- **0x2A0 byte 3:** 123→131, steady +8 over the drive (100 ms). If the common byte−40 scale is right: **83→91 °C** (coolant- or trans-like).
- **0x3C0 byte 0:** 117→128, a bit wigglier; if byte−40: **77→88 °C**. Also coolant-like; rose after hard pulls.
- **0x31F byte 1:** 90→92, very slow; if byte−40: **50→52 °C**, more IAT-like.
- **0x27F byte 5 is a counter** (~+0.5 counts/s, 42→152 in a straight line). Not a temperature.
- **EGT:** not identified. No lagged high-range byte clearly followed the pulls and then cooled.

### Plots

`plots/drive_boost.png` and `plots/drive_exhaust_brake.png`.

---

## Dimmer sweep — both buses, lights ON (2026-11-22)

Two **separate sessions** (do not line the clocks up). Script: key on, headlights on ~5 s, then dimmer wheel slowly min→max→min. He actually swept more than once, starting from the wheel already at max (0xC8).

| File | Frames | Duration | IDs | Rate | Bus |
|---|---:|---:|---:|---:|---|
| `883faa517a95…`.csv → `raw_883f_dimmer.csv` → `pins_3-11_dimmer.csv` | 10,566 | 48.488 s | 84 | 217.9 /s | **CAN-IHS, pins 3/11** (0x322 / 0x2FA / 0x3B2, 0x122 DLC 4) |
| `38dccc865939…`.csv → `raw_38dc_dimmer.csv` → `pins_6-14_dimmer.csv` | 71,077 | 41.813 s | 131 | 1699.9 /s | **CAN-C, pins 6/14** (0x108 / 0x11C / 0x334, 0x122 DLC 6) |

Both files are already in timestamp order.

### The gauge dimming signal — confirmed by script

There are **two** related bytes. This is the missing test from run 2, and it worked.

| Bus | ID | Byte | What it is | Range in this log | Steps | When lights are off |
|---|---|---|---|---|---|---|
| CAN-IHS | **0x2FA** | **byte 6** | **Dimmer wheel / requested level** | **0–200** (147 unique, 1-count steps) | Multi-step ramps down and up several times (e.g. IHS 13.25–14.67, 18.5–20.5, 26.5–28.5, 30.5–32.0) | **Stays at the last wheel position** (200 after lights off at 38.01) |
| CAN-IHS | **0x2FA** | **byte 2** | **Illumination output** | 0, or **34–200** while lights are on (136 unique) | Tracks byte 6 during sweeps, but **floors at 34 (0x22)** | **0** (8.16 lights on 0→200; 38.01 lights off 200→0) |
| CAN-C | **0x334** | **byte 6** | **Illumination output** (BCM copy) | 0, or **34–200** while lights are on (109 unique, 1-count) | Same pattern, separate clock (down 14.59–16.0, up 19–20.5, down 27.5–29.0, up 31.0–31.91) | **0** (lights on 10.69, off 34.73; 0x334 byte 7 = 2 while on) |

The output floor **34 = 0x22** is the same documented Jeep 0x291 minimum; the maximum **200 = 0xC8** matches run 1 / run 2. 0x291 itself is still absent on this truck.

Companion flags (not the level): IHS 0x3B2 byte 0 0x03↔0x83 for lights; IHS 0x2FA bytes 4 and 5 are binary 0/200 (byte 4 goes 0 near wheel-min); CAN-C 0x334 byte 7 = 2 while lights are on, byte 2 2→3.

**For the A-pillar gauges:** use **IHS 0x2FA byte 6** if you want the wheel even with the lights off; use **0x2FA byte 2** or **CAN-C 0x334 byte 6** if you want actual dash illumination (0 when the lights are off, 34–200 when they are on). Average 0x2FA rate in this log is ~111 ms (bursts to 20 ms during ramps).

Plot: `plots/dimmer_sweep.png`. Events: `events_dimmer.csv`.

### Suggested next capture (updated)

Lock / turns / wipers / headlights / IHS brake / dimmer-level are settled enough. Remaining, if wanted:

1. Exhaust-brake **switch** still needs an idle (or parked, engine running) toggle **off / on / auto** — this drive left the switch on the whole time.
2. Optional: a known boost gauge or UDS DID 0x811C alongside 0x30A, to confirm the kPa-gauge scale. A from-cold start would help coolant vs IAT vs trans.

---

## Capture markers (firmware v3, XIAO ESP32-C6)

Click the external button on pad **D5 (GPIO23)** — wire a momentary switch between D5 and GND — or the on-board **BOOT** button (GPIO9). Each debounced press injects one **extended** frame, ID **0x1FFFFFF0**, into the SavvyCAN log (not onto the truck bus). Payload: `AA`, marker number (1, 2, 3…), `millis()` little-endian, `55 55`. The LED blinks three times.

```
python3 /workspace/maker/projects/obd2-sniff/decode/markers.py path/to/savvycan.csv
```

---

## Marker logs — exhaust-brake switch (2026-11-22)

Two back-to-back captures with firmware v3 markers (extended ID 0x1FFFFFF0). The button on the truck **cycles OFF → FULL ON → AUTO → OFF**. The C6 marker counter was **not reset** between buses, so the IHS log is markers 1–12 and the CAN-C log is 15–36. the owner park sequence on CAN-C is the one that nails the switch byte.

The dash HP readout was the reason for marking; he did not send HP numbers with these two logs, so no scale to horsepower is claimed.

### IHS — `pins_3-11_markers.csv`

| Field | Value |
|---|---|
| Source | attachment `e88499e79c5bd1…`.csv → `raw_e884_markers.csv` (md5 `664041a6034d57b973447f1ab619815b`) |
| Bus | **CAN-IHS, pins 3/11** (0x322 present, 0x108 / 0x11C absent, 84 bus IDs, 205.2 frames/s) |
| Length | 32,656 frames, **159.155 s**, already in time order |
| Markers | 12: 27.738, 34.173, 43.938, 117.018, 120.288, 125.923, 127.748, 134.373, 139.523, 143.988, 147.533, 149.873 s |

**Park marks 1–3** (idle, 0 mph, ~700 rpm), interpreted with the cycle order as **OFF, FULL, AUTO**. No IHS byte or 2-bit field holds three stable distinct values across those marks and then holds through the drive. IHS **0x30A is a different message** from CAN-C 0x30A (period ~650 ms, byte 2 stuck at 0xFF). The 3-state switch lives on CAN-C only (see below).

**Drive, 12 marks vs the intended 16:** not a mess of mis-clicks. After an unmarked creep, a first pull to ~25 mph with a **service-brake stop** (0x332 byte 2 bit 6 on 85.74–92.19 s), and a second pull, marks **4–9 are three clean no-pedal braking events**: odd = let-off (speed falls, 0x24E byte 0 = 0, no service brake), even = back on throttle. M6–M7 is a real 1.83 s let-off, not a dead click. **M10** was intended as “switched to AUTO” but is kinematically another let-off (49.8→45 mph, APS-like drops). No IHS switch-byte change is visible, and if mark 3 already left the switch in AUTO, M10 is more likely the first auto-event let-off. **M11** is a brief throttle; **M12** is followed 0.67 s later by a service-brake stop (0x332 bit 6 on at 150.54 s) down to ~6 mph. Marks 13–16 never happened — the log was **cut short**.

Speed on 0x322 bytes 2–3 / 200 mph is **known** (magikh0e / JL) and now **exercised**: 0–55.03 mph (0–88.6 km/h). RPM 0x322 bytes 0–1 big-endian is known. **0x24E byte 0** is an **educated guess** for throttle/APS (0 on every marked let-off, 7–15 during marked throttle); not a public formula. **0x332 byte 2 bit 6** service brake is **confirmed** again (on for the hard stops, off for marks 4–9).

No clean IHS exhaust-brake **effort / HP** byte (nothing in the 30–80 range that is 0 at idle and throttle and high only on no-pedal coasts). 0x2C3 byte 5 as int8 tracks signed accel/decel including service brake — **educated guess**, not EB-specific. **Tow/Haul, educated guess:** IHS 0x30A byte 4 **0xFC→0xFE at 48.261 s** (bit 1), stays 0xFE. Same pattern and the same place in the procedure as CAN-C 0x170 byte 1 (see below). It is **not** the 3-state exhaust-brake switch (only two values; no change at marks 1, 2, 3, or 10). 0x322 byte 4 is an **educated guess** for gear (0x34 Park, 0x2D Reverse, 4/8/12/16 ≈ 1st–4th, bit 0 with service brake).

Plot: `plots/ihs_markers.png`.

### CAN-C — `pins_6-14_markers.csv`

| Field | Value |
|---|---|
| Source | attachment `153cc2a91a49…`.csv → `raw_153c_markers.csv` (md5 `08ece6bcc8803677aa91f20d66d290c8`) |
| Bus | **CAN-C, pins 6/14** (0x108 / 0x11C / 0x120 / 0x30A present, 0x322 absent, 125 bus IDs, 1793.9 frames/s) |
| Length | 237,798 frames, **132.560 s**. One 19 µs backward blip (a marker frame); sort before use. t=0 is still the first row. |
| Markers | 22 total, numbers **15–36**. Park **15–19** at 3.263, 7.283, 12.223, 17.518, 21.698 s = **OFF, FULL, AUTO, OFF, FULL**. Drive **20–27** at 79.793, 87.593, 94.828, 99.773, 105.238, 110.118, 116.163, 120.893 s. **28–36** at 127.243–128.853 s are a deliberate end burst — **ignored**. |

**Exhaust-brake switch, confirmed by markers 15–19: CAN-C 0x30A byte 2.**

| Value | Meaning | When |
|---|---|---|
| **0x00** | OFF | Log start through 6.852 s, and 17.053–21.353 s (marks 15 and 18) |
| **0x80** | FULL ON | 6.852–12.153 s (mark 16) and **21.353 s through the end of the log** (mark 19 and the whole drive) |
| **0xA0** | AUTO | 12.153–17.053 s (mark 17) |

Encoding: bit 7 = not-OFF (FULL or AUTO), bit 5 = AUTO. Changes fall **0.07–0.47 s before** the corresponding marks (he moves the switch, then clicks the marker). Mark 15 has **no byte change** — the switch was already OFF at log start. After mark 19 the byte **stays 0x80 (FULL)**; he never switched to AUTO on this drive, so FULL vs AUTO *while coasting* cannot be compared here.

A companion bit, **0x30A byte 4 bit 1**, is 0 in OFF and 1 in FULL or AUTO (the rest of byte 4 is the previously seen 200 ms 0x40/0x80 toggle). It does **not** tell FULL from AUTO. Use byte 2 for the 3-state.

**0x30A byte 7, confirmed by markers as exhaust-brake effort / active (FULL coasts):** 0 at idle in every switch state, 0 whenever APS is up, then **47–58** starting 0.2–0.8 s after each marked let-off (max 58, 56, 55, 52 on the four FULL coasts). Non-zero is the “active” condition; no cleaner binary flag was needed. Because the drive stayed FULL, there is **no AUTO-coast byte-7 sample** on this log.

**Drive marks 20–27 (script shortened, no AUTO portion):**

| Mark | t (s) | Interpretation |
|---|---|---|
| 20 | 79.79 | Mid first pull (25.8 mph, APS 58%, b7=0). Not a let-off and not the AUTO switch. Extra click during the accel that started ~73.7 s. |
| 21 | 87.59 | Let-off 1 (FULL). APS already 0 (~86.52). b7 rises to 54–58. Coast 51.2→41.5 mph. No service brake. |
| 22 | 94.83 | Back on throttle (APS 46%, b7=0). |
| 23 | 99.77 | Let-off 2 (FULL). b7 to 51–56. Coast 50.5→40.4 mph. |
| 24 | 105.24 | Back on throttle. |
| 25 | 110.12 | Let-off 3 (FULL). b7 to 49–55. Coast 49.3→39.5 mph. |
| 26 | 116.16 | Back on throttle. |
| 27 | 120.89 | Let-off 4 (FULL). b7 to 47–52. No closing throttle mark. Service brake at 126.82 s, then the 28–36 burst. |

Speed 0–84.3 km/h (0–52.4 mph), RPM 646–2604, APS 0–60.0%. First motion 27.03 s. Unmarked driveway work with service brake occupies ~24–73 s (0x140 0x85 / 0x11C pedal).

**Tow/Haul, educated guess (strengthened):** 0x170 byte 1 **252→254 at 29.050 s**, stays 254. Same bit and the same place in the procedure as IHS 0x30A byte 4 at 48.26 s on the other log (after the park switch clicks, as he starts to roll).

Plot: `plots/canc_markers.png`.

### IHS vs CAN-C

- The **3-state exhaust-brake switch is on CAN-C only**: **0x30A byte 2 = 0x00 / 0x80 / 0xA0**. It is not on IHS (same ID, different message).
- **Effort / active** is CAN-C **0x30A byte 7**, confirmed on FULL closed-throttle coasts. No equivalent byte was found on IHS, and this CAN-C drive never entered AUTO, so FULL vs AUTO effort is still uncompared. A short AUTO-coast pass on pins 6/14 would close that.
- **Tow/Haul** candidates line up across the two buses and the two sessions (CAN-C 0x170 b1, IHS 0x30A b4, both bit 1, FC/252 → FE/254 after the park clicks). Still an educated guess until he confirms he hit Tow/Haul.

## Brake-HP voice calibration (Nov 22, 2026, log raw_e4df_bhp.csv / pins_6-14_bhp_voice.csv)
Sync taps: markers 37-39 and 40-42. Reading taps 43-53; 54-59 end burst. Spoken values (voice call, in order): 75, 66, 60, 0, 60, 59, 60, 70, 54, 50, 48.
Exhaust-brake coasts (0x30A b7 >0): 86.0-90.4 s peak 57; 92.8-96.1 peak 53; 104.3-106.6 peak 46 (flat); 109.6-116.7 peak 52 decaying to 35.
Best fit (educated guess, ~1 s reaction lag): brake HP ~= 0x30A b7 x 1.32. Examples: 57->75, 50->66, 45->60 (coast 1, tap 45/46); flat 46 -> 60/59/60 (coast 3); 52->70, 41->54, 38->50, 36->48 (coast 4, taps 49-52). Not all spoken numbers had taps; spread about +/-3 hp.

---

## Fuel rail pressure and engine load (Gauge 3, 2026-11-22)

For the 3-gauge A-pillar hub on CAN-C (pins 6/14, 500 kbit/s). VGT is dropped. Logs re-checked: `pins_6-14.csv`, `_run2`, `_drive`, `_dimmer`, `_markers`, `_bhp_voice`. No 0x7DF / 0x7E0 / 0x7E8 diagnostic traffic in any of them (listen-only captures).

**2017 Ram 3500 is pre-SGW (high confidence).** Ram announced the Security Gateway on **2018MY** trucks ([NTEA / Trailer Body Builders, Dec 2017](https://www.trailer-bodybuilders.com/truck-bodies/article/21742501/ram-highlights-technical-changes); [youcanic SGW list](https://www.youcanic.com/fca-sgm/) lists 2018+ Ram 1500 and HD 2500/3500). FCA describes SGW as “most 2018 model year and later.” A 2017 HD should answer ordinary Mode 01 / 0x22 read requests at the OBD port without a bypass. Even on SGW trucks, emissions Mode 01 is typically still allowed; enhanced writes are what get blocked.

### Broadcast hunt (educated guesses only)

**Fuel rail pressure — no convincing broadcast candidate.** A 16-bit signal that is ~4,000–5,000 psi (≈ 30 MPa) at idle and rises toward ~26,000+ psi with load was not found. Apparent range hits were false alignments: 0x2FC / 0x134 bytes that simply copy APS, 0x2A8 byte 2 (the gear candidate) paired with a neighbor, and 16-bit fields with ~0 correlation to APS/RPM or that saturate at 0xFFFF. Public 2013–2018 6.7 tools also treat rail pressure as a **requested** PID, not a documented broadcast (same pattern as boost / DID 0x811C).

**Engine load — best broadcast candidate, educated guess: CAN-C 0x101 byte 2**, 100 ms, DLC 5 (byte 0 stuck 0x42; byte 3 is an APS copy of 0x120 byte 2). On the drive log: idle/stopped median 5 (range 0–23), **0 on closed-throttle coasts**, median 131 and max **207** with APS ≥ 40% (corr 0.96). If the scale is the SAE load formula `byte * 100 / 255`, that is ~2% at idle and ~81% on the hardest pull. It is **not** a raw 0–100 byte and is **not** confirmed against PID 0x04. 0x2A0 byte 2 (earlier load/VGT guess) stays mid-high on coasts, so it is a worse “load % tracking APS/torque” for a gauge. Use 0x120 byte 2 only as known APS (pedal), not load.

### What to poll (sourced; do not invent DIDs)

Request on 11-bit **0x7DF** (functional) or **0x7E0** (ECM physical); responses **0x7E8**. Standard formulas: [SAE J1979 / CSS Electronics PID table](https://www.csselectronics.com/pages/obd2-pid-table-on-board-diagnostics-j1979), [Wikipedia OBD-II PIDs](https://en.wikipedia.org/wiki/OBD-II_PIDs).

| Request | What | Formula | Why this truck |
|---|---|---|---|
| `01 00 / 20 / 40 / 60` | Supported-PID bitmaps | 4-byte bitfield | Always start here. 0x6D is in the 0x60 block. |
| `01 04` | Calculated engine load | `A * 100 / 255` % | SAE says compression-ignition engines shall support PID 0x04. |
| `01 43` | Absolute load | `A * 100 / 255` % | Standard; support is up to the bitmap. |
| `01 61` | Driver’s demand % torque | `A - 125` % | Standard, and in the [Cummins 6.7 Torque Pro list](https://mopar1973man.com/cummins/articles.html/4th-generation/engine/electrical/using-torque-pro-for-gauges-extended-pid-sensors-r577/) as mode/PID `161`. |
| `01 62` | Actual engine % torque | `A - 125` % | Standard companion to 0x61. |
| `01 23` | Fuel rail **gauge** pressure | `(A*256+B) * 10` kPa | The diesel/GDI rail PID (up to 655,350 kPa / ~95,000 psi). |
| `01 6D` | Fuel pressure control: **commanded and actual** | Byte 0 = support bits; B,C commanded A; D,E actual A; F temp A (`F-40` °C); `* 10` kPa. In psi: `* 1.45` | Same Torque Pro 6.7 list uses `016D` with `(B*256+C)*1.45` psi commanded and `(D*256+E)*1.45` actual, max 27,000 psi. This is the way to get **both** commanded and actual on one request. ISO-TP multi-frame (payload is longer than 7 bytes). |
| `01 22` | Rail pressure *relative to manifold* | `(A*256+B) * 0.079` kPa, max ~5,177 kPa / **~751 psi** | Too small for a 26,000 psi common rail. Probe it only to see if the bit is set; do not use it for Gauge 3. PID 0x0A (max 765 kPa) is likewise a low-side / gas rail, not the HP rail. |

**No sourced Mode 22 FRP/load DID** for 2013–2018 Ram 6.7 was added. Banks iDash documents FRP / FRP commanded as parameters on a 2015 6.7 ([parameter list PDF](https://banks-assets.s3.amazonaws.com/sdc/manual/2015_RAM_67_CUMMINS.pdf)) but does not publish a DID. The mopar1973man Mode 22 scratch list (0x9000, 0x9E00, …) is unidentified idle dumps, not FRP. Do not invent 0x22xx IDs. Previously sourced `22 81 1C` is boost, not rail pressure.

Support on this exact CM2350B is **not proven until the probe runs**. 0x04 is the most likely to exist; 0x6D is well attested in the 4th-gen Cummins Torque pack but still needs a yes from PID 0x60 and a live response.

### Transmitting (no longer listen-only)

Polling at a few hertz is what every scan tool does and is normal on the diagnostic addresses. It **does** mean the hub must transmit: leave listen-only, and **wire SN65HVD230 CTX to D3 / GPIO21** (not 3V3). Risks, kept small if we only send read services 0x01 / 0x22: a little extra bus load (a few frames/s vs ~1,600–1,800 already on CAN-C — negligible), possible NRCs or timeouts, ISO-TP flow-control must be answered promptly for PID 0x6D. Do not send writes (0x2E), routines (0x31), or security access. Recommended poll rate for the hub later: **2–5 Hz** for 0x6D + one load PID (0x6D is multi-frame); do not spin at 50–100 Hz. The `pid_probe` sketch itself is **one-shot** and stops.

**Quick test:** flash `firmware/arduino_ide/pid_probe_XIAO_C6_v1.zip` after the CTX rewire, key ON / engine idle, Serial 115200. It queries the bitmaps plus the PIDs above, prints raw + decoded values, and halts.

## v4 drive — polled PIDs + broadcast (2026-11-22, `pins_6-14_v4drive.csv`, raw `raw_df13_v4drive.csv`)

Firmware `sniffer_node_C6_v4`, CAN-C pins 6/14, 218 s, 406,932 frames (~1,860 fps). Exhaust-brake switch (0x30A b2) = 0x80 **FULL for the whole log**; Tow/Haul bit (0x170 b1 bit 1) goes 0 → 1 at t = 71 s, which **confirms that bit** (was an educated guess). Scripts: `pid_poll_decode.py` (events / wide / poll-stats CSVs in `pid_poll/`) and `v4drive_decode.py` (events + plots).

### Poll performance
| PID | requests | replies | missed | req Hz | reply ms (median / max) |
|---|---|---|---|---|---|
| 6D | 834 | 802 | 32 | 3.82 | 8.4 / 12.3 (to last CF) |
| 73 | 844 | 844 | 0 | 3.87 | 3.4 / 7.5 |
| 87 | 839 | 839 | 0 | 3.85 | 3.5 / 7.2 |
| 74 | 837 | 837 | 0 | 3.84 | 3.4 / 7.2 |
| 04 | 834 | 834 | 0 | 3.83 | 3.5 / 7.4 |
| 62 | 832 | 832 | 0 | 3.82 | 3.4 / 6.7 |
| 5E | 832 | 833 | 0 | 3.81 | 3.5 / 7.2 |
| 0C | 826 | 826 | 0 | 3.78 | 3.4 / 7.0 |

- Slots are exactly 31 ms (median request gap 31.0 ms, cycle 248 ms = 4.03 Hz max). ~30.6 req/s achieved.
- **367 of ~7,045 slots (5.2 %) had no request on the bus** (gap 62 ms instead of 31): the single-shot TX failed 3 quick tries in a row. Likely lost arbitration (0x7E0 is near the lowest priority on a ~45 %-loaded bus) and the 3 tries fall inside the same burst — educated guess.
- **All 32 6D misses** = First Frame received but **no flow control on the bus** (same TX failure, for the FC frame). The ECM then aborts; the next request is answered normally. Every request that *did* go out was answered (0 timeouts on single-frame PIDs).
- Fix for v5 (not done): retry single-shot with 1–3 ms spacing and more tries (e.g. 6–8), give FC priority; or normal retransmit for poll frames once the bus is known awake.

### Events (P = boost pull, B = exhaust-brake event, b7 > 0)
| Event | t (s) | Boost bc / MAP 87 | EBP 73 | Turbo rpm | rpm | FRP cmd / act max | Load / torque | Fuel gal/h |
|---|---|---|---|---|---|---|---|---|
| P1 | 75.0–77.3 | 15.7 / 16.0 | 20.0 | 89,260 | 2,310 | 20,951 / 21,103 | 41 / 56 % | 11.6 |
| P2 | 95.6–105.6 | 25.8 / 26.0 | 35.8 | 108,740 | 2,734 | 25,122 / 24,200 | 62 / 82 % | 18.8 |
| P3 | 113.9–116.2 | 13.1 / 13.4 | 18.2 | 77,280 | 1,580 | 18,330 / 18,578 | 53 / 67 % | 9.9 |
| P4 | 122.5–124.3 | 16.7 / 17.4 | 22.7 | 87,180 | 1,982 | 20,588 / 20,682 | 46 / 62 % | 11.0 |
| P5 | 179.8–189.0 | 23.1 / 23.2 | 28.0 | 102,030 | 2,533 | 22,565 / 22,301 | 54 / 71 % | 14.9 |
| P6 | 197.7–201.5 | **32.3 / 32.6** | **51.0** | **112,230** | 1,980 | **25,696 / 25,524** | **82 / 100 %** | 16.8 |

| Event | t (s) | mph | rpm | b7 max (HP est.) | EBP 73 median / max | Boost | Turbo median | FRP cmd / act median |
|---|---|---|---|---|---|---|---|---|
| B1 | 106.9–112.1 | 62 → 53 | 1,435–1,693 | 61 (81) | 61.4 / **66.0** | 2.3 | 21,400 | 7,725 / 6,556 |
| B2 | 117.7–121.0 | 56 → 50 | 1,373–1,518 | 48 (63) | 55.9 / 57.6 | 1.5 | 29,800 | 7,203 / 6,006 |
| B3 | 125.5–133.7 | 53 → 39 | 1,383–1,872 | 62 (82) | 57.3 / 58.9 | 2.3 | 34,400 | 7,867 / 7,439 |
| B4 | 136.7–139.0 | 35 → 31 | 1,450–1,677 | 53 (70) | 55.8 / 56.6 | 1.9 | 34,500 | 7,636 / 7,725 |
| B5 (0.6 s) | 142.5–143.1 | 27 → 24 | 1,109–1,299 | 35 (46) | 49.1 / 49.7 | 1.5 | 29,400 | 6,244 / 6,428 |
| B6 | 190.1–196.1 | 53 → 40 | 1,409–1,858 | 62 (82) | 58.0 / 59.0 | 2.3 | 36,000 | 7,874 / 6,937 |
| B7 | 202.4–210.3 | 53 → 39 | 1,391–1,887 | 65 (86) | 57.7 / 59.8 | 2.6 | 36,600 | 7,946 / 7,621 |
| B8 (0.8 s) | 213.4–214.2 | 35 → 33 | 1,555–1,652 | 53 (70) | 56.3 / 56.3 | 2.0 | 38,100 | 7,825 / 8,024 |

### FRP (PID 6D)
- Overall 4,302–25,696 psi. **Idle** (parked): cmd 4,696 / act 4,635 median, act 4,456–4,786. **Cruise** (no boost, no brake): ~7,000–9,000, median ~8,000. **Exhaust brake / coast**: cmd ~7,750, act ~7,170 (act sits ~600 psi under cmd with no fuel injected).
- Deviation cmd − act: steady-state p95 1,384 psi, max 2,586; at high load median +371. The 21 samples > 2,500 psi (max 7,484 at t = 121.8) are all tip-in / tip-out steps lasting ≤ 0.5 s.

### MAP 87 vs broadcast boost → 87 scale confirmed
MAP (1/32 kPa − 97) − boost (b6 × 0.145): median +0.33 psi, p5/p95 −0.14 / +0.87, slope 0.999, r = 0.998. Raw 87 runs 3,108–10,305 → 97.1–322 kPa abs at 1/32 (x0.01 would give 31 kPa abs at idle = impossible). **1/32 kPa confirmed.** Either source works for boost; broadcast b6 is 50 Hz vs 4 Hz.

### PID 73 verdict — x0.01 kPa abs is plausible (upgraded from "check")
Raw 10,003–55,209 → 100.0–552 kPa abs → **~0.4–1.2 psi gauge at idle, ~9 at cruise, 18–51 on pulls (drive pressure ~1.4–1.6 × boost), 55–60 psi plateau under full exhaust brake, peak 66.0**. Alternatives fail: 1/32 kPa → 31 psi gauge at idle; 1/128 → below atmospheric at idle. Reference (forum, anecdotal, not a spec): a 6.7 owner's scan tool read 100 kPa abs at idle, ~37 psi drive at 26 psi boost, and 565 kPa abs (~67 psi gauge) max on the exhaust brake — practically the same as this truck. Still not checked against a mechanical gauge (no EBP tap), so "plausible", not "verified". Baro is assumed 97 kPa (PID 33 earlier); poll 0x33 occasionally if driving at altitude.

### b7 vs EBP
During braking EBP pins at ~55–60 psi (the ECM's target) while b7 keeps varying: b7 vs EBP r = 0.65, vs rpm r = 0.71 (b7 ≈ 0.052 × rpm − 32 with EBP on the plateau), vs EBP × rpm r = 0.79. So **b7 behaves like braking power (pressure × flow), not pressure** — consistent with it being the dash HP. Max this drive 65 counts ≈ 86 hp (1,400–1,900 rpm, light truck); the earlier 0–230 dash calibration was not exceeded.

### Load verdict — 0x101 b2 **is** PID 04 raw (load % × 2.55)
Nearest-sample comparison (834 pairs): b2 = 0.998 × A + 0.14, r = 0.996, 58 % identical, 93 % within 3 counts (differences are sampling time on transients). Broadcast max 210 = 82.4 %, same as PID 04 max 82.0 %. **Use 0x101 b2 / 2.55 for the load gauge (50 Hz broadcast, no polling).** Torque (62) tracks load (r = 0.99) but reads ~1.2–1.3× load %; idle load 3.5 %, torque 9 %, fuel 0.53 gal/h; peak torque 100 % / fuel 18.8 gal/h. Turbo 14,600 rpm idle, ~33,000 on the brake, 112,230 peak.

### Recommended gauge ranges (observed data; references labelled)
- **Gauge 2, boost mode:** 0–40 psi dial, source 0x30A b6. Observed max 32.3 psi at 82 % pedal (not WOT); earlier log peak 242 kPa ≈ 35 psi. Caution 35, red 38+ (educated guess just above observed stock behaviour; forum lore puts the stock VGT's useful limit around 30–40 psi — not a spec).
- **Gauge 2, exhaust-brake mode — real EBP (recommended primary):** 0–80 psi dial, source PID 73. Normal brake plateau 55–60, observed peak 66. Caution 65, red 70+ (anecdotal: forum reports of ~67 psi max on stock 6.7s; drive pressure > ~65 psi is commonly called hard on the stock turbo/manifold). Show HP (b7 × 1.32, 0–230 like the dash) as a secondary readout/arc, because EBP saturates while HP tells how hard it's braking.
- **Gauge 3, FRP:** 0–30 kpsi dial, cmd as a bug/needle and actual as the main needle. Observed 4.3–25.7 kpsi. Red 27 kpsi+ (Torque Pro 6.7 PID pack lists 27,000 psi as max — reference, not an OEM spec). Low warning: actual < 4,000 psi with engine running (idle min seen 4,456). Deviation warning: |cmd − act| > 2,500 psi for > 1 s (all observed > 2,500 excursions were < 0.5 s).
- **Gauge 3, load:** 0–100 %, source 0x101 b2 / 2.55; observed 0–82 %. No redline (informational).

### Plots
- `plots/v4drive_overview.png` — boost / MAP / EBP, FRP cmd vs act, turbo + rpm + HP + speed, load (04 vs 0x101) + torque + fuel; events labelled, brake hatched.
- `plots/v4drive_exhaust_brake.png` — EBP vs HP estimate through B1–B8, HP vs EBP and HP vs rpm scatters.
- `plots/v4drive_crosschecks.png` — MAP 87 vs boost b6, 0x101 b2 vs PID 04 raw, FRP actual vs commanded.
- `plots/pins_6-14_v4drive_pid_poll.png` — generic decoder output.
