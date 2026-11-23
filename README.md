# diesel-can-gauges

Open-source CAN sniffer and a set of three round digital gauges for a **2017 Ram 3500 with the 6.7 Cummins**. The truck has no secure gateway, so a listen-only (and, optionally, a read-only Mode 01) tool can talk on the OBD-II diagnostic bus.

This repository is a snapshot of a working bench-and-truck sniffer plus starter firmware for an ESP-NOW gauge cluster. It is not a commercial product and is not affiliated with Stellantis, Ram, Cummins, or Waveshare.

| Folder | Contents |
|---|---|
| [`obd2-sniff/`](obd2-sniff/) | XIAO ESP32-C6 (or ESP32-S3) + SN65HVD230 CAN sniffer. GVRET/SavvyCAN output, optional Mode 01 PID poller, decode scripts, wiring, and decoded summaries. |
| [`gauges/`](gauges/) | Three Waveshare ESP32-S3-Touch-LCD-2.1 round gauges (EGT, boost/EBP, rail/utility) fed over ESP-NOW from an ESP32-S3 hub. Older single 1.28 in EGT sketch is kept for reference. |
| [`sniffer-case/`](sniffer-case/) | 3D-printable two-piece case for the wired XIAO C6 + WCMCU-230 sniffer (CadQuery source, STL, STEP). |
| [`gauge-pod/`](gauge-pod/) | Placeholder for a future A-pillar 3-gauge pod. |

Start with [BUILD_GUIDE.md](BUILD_GUIDE.md) and [BOM.md](BOM.md).

## What it does

**Sniffer.** A Seeed XIAO ESP32-C6 and a WCMCU-230 (SN65HVD230) transceiver tap OBD-II **pins 6/14 (CAN-C, 500 kbit/s)**. Ground is pin 5 (and pin 4). The v3 listen-only build ties CTX to 3.3 V so the transceiver cannot drive the bus. The v4 sniffer adds a polite, read-only Mode 01 poller, so CTX must go to **D3 / GPIO21** and CRX to **D4 / GPIO22**. Frames go out over USB as GVRET, which [SavvyCAN](https://www.savvycan.com/) understands. A second bus (pins 3/11, IHS, 125 kbit/s) can be sniffed with another node and an ESP-NOW gateway.

Decoded Mode 01 PIDs on this ECM (replies on 0x7E8; the TCM on 0x7E9 also answers some requests):

| PID | Meaning | Notes |
|---|---|---|
| 0x6D | Fuel rail pressure, commanded and actual | Multi-frame (ISO-TP flow control required), ×10 kPa |
| 0x73 | Exhaust pressure (EBP) | ×0.01 kPa absolute, minus an assumed 97 kPa baro. Plausible, **not verified** against a mechanical gauge |
| 0x87 | MAP | 1/32 kPa absolute (confirmed) |
| 0x74 | Turbo RPM | ×10 |
| 0x04 | Calculated engine load | Also present as broadcast 0x101 byte 2 (raw = load % × 2.55) |
| 0x62 | Actual engine torque | |
| 0x5E | Fuel rate | |
| 0x0C | Engine RPM | |

Useful broadcast IDs on CAN-C: **0x101** (load), **0x30A** (byte 2 exhaust-brake switch, byte 6 boost kPa gauge, byte 7 brake effort; HP ≈ byte 7 × 1.32), **0x170** (Tow/Haul). Factory EGT PIDs 0x78 and 0x98 read a fixed 150 °C placeholder on this ECM and are not used.

**Gauges (planned cluster).**

1. EGT from a cylinder-6 pre-turbo K-type probe (MAX31855 on the hub).
2. Boost 0–40 psi under throttle. During the exhaust brake this face switches to real EBP from PID 0x73 (0–80 psi) with brake HP as a secondary line.
3. Fuel rail pressure, actual vs commanded, plus load, intake air, battery, and pitch/roll.

The hub talks to the three displays over ESP-NOW. **Flash the hub and all three displays together** (packet version must match). A mismatched display shows NO SIGNAL.

## Status and roadmap

**Works today (tested on a 2017 Ram 3500 6.7 Cummins):**

- Wired XIAO ESP32-C6 sniffer on CAN-C (pins 6/14) and IHS (pins 3/11)
- GVRET capture in SavvyCAN
- v4 Mode 01 poller at about 4 Hz per PID, with ISO-TP flow control for 0x6D
- Decode scripts and plots of those sessions (summaries are in the repo; raw logs are not)

**Not yet tested on the truck:**

- Gauge hub and the three Waveshare 2.1 in displays (boards compiling; screens were still on order when this was published)
- ESP-NOW range with the hub under the dash
- PID 0x73 scale vs a mechanical exhaust-back-pressure gauge
- QMI8658 pitch/roll signs once the displays are in a pod
- In-cab heat and long-term current draw of the finished cluster

**Known limits / educated guesses:**

- Gauge warn and alarm redlines (EGT, boost, EBP, rail lag, battery, IAT) are **starting estimates**, not factory specifications. Edit them before you rely on them.
- About 5 % of poll requests lost arbitration on a ~45 % busy bus (0x7E0 is low priority). A later hub revision should space retries 1–3 ms apart, 6–8 tries.
- Alarm values and COOL AT (older 1.28 in firmware) should be tuned on the vehicle.

**Roadmap**

1. Flash and bench-test the hub + three displays (`sweep on`), then a parked-truck PID `probe`.
2. Short drive to confirm boost / EBP / rail / brake-HP faces against the dash.
3. Add the A-pillar 3-gauge pod CAD (`gauge-pod/`, coming later).
4. Optional: tighter poller retries, and a Mode 22 table only from a trusted source (the firmware will not guess those addresses).

## Safety

Working on a vehicle CAN bus can affect safety-critical systems. Read the full disclaimer in [BUILD_GUIDE.md](BUILD_GUIDE.md#safety-and-disclaimer). Short version:

- The listen-only sniffer (CTX tied to 3.3 V, TWAI listen-only) does not transmit, ACK, or send error frames.
- The v4 poller and the gauge hub **do** transmit, but only standard OBD-II Mode 01 read requests (and ISO-TP flow control for multi-frame replies). They do not write to the ECU, do not clear codes, and do not send security-access or routine-control services.
- Disconnect this hardware before anyone flashes a tuner or scan tool.
- Use at your own risk. Not affiliated with Stellantis, Ram, or Cummins.

## License

MIT. See [LICENSE](LICENSE). Waveshare display driver files under `gauges/firmware/gauge3_v1/display/` are adapted from the official ESP32-S3-Touch-LCD-2.1 demo; see that folder and [Waveshare’s documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.1/Resources-And-Documents).
