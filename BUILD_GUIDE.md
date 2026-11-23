# Build guide

How to reproduce the sniffer, the decode toolchain, and the three-gauge cluster. This is a bench-and-truck project. Do the first hook-up with the key OFF and the parking brake on. If a step was only verified on one 2017 Ram 3500 6.7 Cummins, it is marked as such.

Parts and approximate prices: [BOM.md](BOM.md). Print settings for the sniffer case are in the [Printing](#printing) section.

## Safety and disclaimer

**Working on a vehicle CAN bus can affect safety-critical systems (engine, transmission, brakes, airbags).** A wiring mistake, an extra terminator, or a second tool on the bus during an ECM flash can damage modules or change how the truck behaves.

- **Listen-only is the default for a first capture.** For the v3-style sniffer, run the TWAI controller in listen-only mode and tie the SN65HVD230 CTX pin to 3.3 V so the transceiver cannot drive the bus.
- **The v4 sniffer and the gauge hub poll only standard OBD-II Mode 01** (service 0x01) read requests, plus ISO-TP flow-control frames that the ECM expects after a multi-frame reply. They do not write to the ECU (no 0x2E), do not start routines (no 0x31), do not change sessions, do not clear DTCs, and do not perform security access. SavvyCAN “send frame” is ignored by the sniffer firmware.
- **Disconnect this hardware** before a tuner or another scan tool flashes the ECM. Two devices on the bus during a write can brick the module.
- **OBD-II pin 16 is battery +12 V at all times.** If you power a board from pin 16, put a 1 A fuse first. For the gauge hub, use an ignition-switched add-a-circuit fuse tap so the cluster cannot drain the battery.
- **Remove or cut the 120 Ω termination resistor** on each SN65HVD230 module before connecting to the truck. The bus is already terminated (about 60 Ω between CAN-H and CAN-L). An extra 120 Ω drops that to about 40 Ω.
- **Do not stare at the gauges while driving.** Set the buzzer so you can hear an alarm.
- **Alarm thresholds in the firmware are estimates, not factory limits.**
- This project is unofficial and **is not affiliated with Stellantis, Ram, Cummins, or Waveshare.**
- **Use this hardware and software at your own risk.** There is no warranty. See [LICENSE](LICENSE).

## 1. Hardware overview

Two related builds share the same OBD-II pinout:

1. **USB sniffer** — Seeed XIAO ESP32-C6 + WCMCU-230 (SN65HVD230), optional printed case, laptop running SavvyCAN. This is the path that has been used on the truck.
2. **Gauge cluster** — ESP32-S3-WROOM-1 N16R8 hub (CAN + EGT amp + EC11 knob) talking ESP-NOW to three Waveshare ESP32-S3-Touch-LCD-2.1 boards. Compiled, **not yet run on the truck.**

### OBD-II pinout (SAE J1962)

Looking at the vehicle connector (the one in the cab):

| Pin | Function | This project |
|---|---|---|
| 4 | Chassis ground | Tie to module GND (optional, with pin 5) |
| 5 | Signal ground | **Required GND** |
| 6 | CAN-H (CAN-C) | 500 kbit/s powertrain / diagnostic |
| 14 | CAN-L (CAN-C) | Pair with pin 6, twisted |
| 3 | CAN-H (IHS, maker use on this truck) | 125 kbit/s body bus, optional second node |
| 11 | CAN-L (IHS) | Pair with pin 3 |
| 16 | Battery +12 V, always on | Only through a 1 A fuse, if you use it at all |

On the 2017 Ram 3500 that this was developed against, key-off ohms were about 61.5 Ω on 6/14 and 62.8 Ω on 3/11. Key-on recessive voltages were roughly 2.3–2.7 V. Other pins read 0 V.

## 2. Sniffer wiring (XIAO ESP32-C6)

Use a WCMCU-230 or similar 3.3 V SN65HVD230 module. Power the module from the XIAO 3V3 pin, never 5 V or 12 V.

| WCMCU-230 pin | XIAO ESP32-C6 | Truck |
|---|---|---|
| 3V3 | 3V3 | — |
| GND | GND | OBD pin 5 (and 4) |
| CRX (R) | **D4 / GPIO22** | — |
| CTX (D) | See below | — |
| CANH | — | OBD pin 6 (or 3 for IHS) |
| CANL | — | OBD pin 14 (or 11 for IHS) |

**CTX depends on the firmware:**

- **Listen-only (v3 and earlier, or any build that must not drive the bus):** tie CTX to **3.3 V**. The firmware TX pin is unused. This is what [obd2-sniff/wiring](obd2-sniff/wiring/) schematic v3 shows.
- **v4 sniffer with the Mode 01 poller, or the gauge hub:** wire CTX to **D3 / GPIO21**. The controller runs in normal mode so it can ACK and send read requests.

Optional: a momentary switch from XIAO **D5 / GPIO23** to GND injects a marker frame (extended ID 0x1FFFFFF0) into the GVRET stream only — it is not placed on the truck bus. BOOT (GPIO15 is the LED; BOOT is GPIO9) is a second marker input on the v4 C6 build.

Keep CAN-H/CAN-L a twisted pair. The printable case (`sniffer-case/`) is 50 × 35 × 20 mm and has a USB-C opening and a three-wire exit.

A multi-node ESP-NOW layout (one ESP32-S3 per bus, plus a USB gateway) is documented in `obd2-sniff/README.md`. The C6 v4 sketch can also speak GVRET directly over its own USB port, which is the simpler one-bus setup.

## 3. Flash the sniffer (Arduino IDE)

Software used to compile this tree: **Arduino-ESP32 core 3.3.12**. Newer 3.x cores will probably work; if a compile breaks, pin the core to 3.3.12.

### One-time IDE setup

1. Install [Arduino IDE 2](https://www.arduino.cc/en/software).
2. Add the ESP32 board package. In File > Preferences, add this Additional Board Manager URL:

   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`

3. Tools > Board > Boards Manager > install **esp32** by Espressif (3.3.12).
4. No extra libraries are required for the sniffer or the PID probe. (The gauge hub needs libraries; see section 7.)

### XIAO ESP32-C6 (recommended, v4 poller)

1. Open `obd2-sniff/firmware/arduino_ide/sniffer_node_C6_v4/sniffer_node_C6_v4.ino`.
2. Tools > Board > XIAO_ESP32C6 (Seeed nRF52 / ESP32 boards may list it as “XIAO_ESP32C6”).
3. Tools > USB CDC On Boot: **Enabled**. Leave other options at defaults.
4. Plug in the XIAO over USB-C. Hold BOOT and tap RESET if the port does not appear.
5. Upload.
6. Serial Monitor, 115200, newline. Type `h` for help. Useful commands: `s` stats, `q` poll stats, `p1` / `p0` poller on/off (saved), `n0` node id, `f1` fake-bus bench mode.

The v4 sketch starts polling about 3 s after the bus is heard. It only sends Mode 01 requests and ISO-TP flow control, and it pauses if it sees another tester on 0x7DF/0x7E0.

### PID probe (one-shot)

`obd2-sniff/firmware/arduino_ide/pid_probe_C6_v2/` walks supported Mode 01 PIDs and prints raw plus decoded values, then stops. Same board settings and the same CTX=D3 / CRX=D4 wiring. Use this once to confirm the ECM answers 0x6D, 0x73, 0x87, and the rest.

### ESP32-S3 node + gateway (optional multi-bus)

Board: **ESP32S3 Dev Module**, USB CDC On Boot **Enabled**.

- N16R8 node: Flash Size 16 MB, PSRAM **OPI PSRAM**.
- N8R2 node: Flash Size 8 MB, PSRAM **QSPI PSRAM**.
- Gateway: any S3; CDC on boot enabled.

Sketches: `obd2-sniff/firmware/sniffer_node/` and `obd2-sniff/firmware/gateway/`. Default pins are GPIO4 (TX) and GPIO5 (RX). A XIAO ESP32-S3 variant is under `obd2-sniff/firmware/arduino_ide/sniffer_node_S3/`. After editing shared headers, run `obd2-sniff/tools/sync_common.sh`.

arduino-cli equivalents (core 3.3.12):

```bash
arduino-cli core install esp32:esp32@3.3.12
arduino-cli compile -b esp32:esp32:XIAO_ESP32C6:CDCOnBoot=cdc obd2-sniff/firmware/arduino_ide/sniffer_node_C6_v4
```

## 4. SavvyCAN / GVRET

1. Install [SavvyCAN](https://www.savvycan.com/) (free). Close any Serial Monitor on the same port.
2. Connection > Open Connection Window > Add New Device Connection > **Serial Connection (GVRET)** > pick the XIAO or gateway port > Create.
3. You do not set bus speeds in SavvyCAN; the firmware does that. “Send” is ignored.
4. Useful views: RE Tools > Sniffer, Flow View, Frame Info. File > Save Log File writes a GVRET CSV.
5. Without SavvyCAN, `python3 obd2-sniff/tools/gvret_check.py <PORT>` prints frames/s (`pip install pyserial`).

On the USB gateway layout, buses 0–4 are nodes and bus 5 is gateway status. A C6 plugged in directly is a single GVRET device.

**Capture tip:** truck in PARK, parking brake on. Mark each action (or use the D5 marker button) and wait about 10 s. Do not commit raw vehicle logs to git; they can contain identifying data.

## 5. Running the PID poller

With v4 flashed and CTX on D3:

1. Key ON or engine running. The bus must already be awake; the poller will not wake a sleeping bus.
2. Watch Serial or SavvyCAN. Requests go to 0x7E0 (and a 0x7DF wake-up), replies from 0x7E8 (ECM) and sometimes 0x7E9 (TCM).
3. Slot time is 31 ms, about 4 Hz per PID, a few percent of bus load.
4. `p0` disables the poller (saved in flash) if you want a quiet listen pass — but CTX is still able to drive the bus in v4, so for a hardware listen-only pass move CTX back to 3.3 V and use a listen-only build.

## 6. Decode scripts

Python 3, from `obd2-sniff/decode/`:

```bash
pip install pandas numpy matplotlib
# optional, for serial tools:
pip install pyserial
# optional, to rebuild the printable pin-ID PDF:
pip install reportlab
```

Scripts expect a SavvyCAN/GVRET CSV in a local `data/logs/` folder (that folder is gitignored). They write plots under `decode/plots/` and small event tables next to the scripts.

| Script | Role |
|---|---|
| `decode_logs.py` | ID tables and basic plots from a capture |
| `pid_poll_decode.py` | Rebuild PID time series from a v4 poller log |
| `drive_decode.py`, `v4drive_decode.py`, `canc_marker_decode.py`, `marker_decode.py`, `dimmer_decode.py`, `run2_decode.py` | Session-specific decoders used to produce the checked-in plots and event CSVs |

This repo ships **decoded summaries and plots only**, not the raw multi-megabyte CSVs. Re-run a script against your own log to reproduce a plot. A short README in `obd2-sniff/decode/` explains the ID map.

## 7. Gauge hub and three displays

**Flash the hub and all three displays with matching firmware.** The ESP-NOW packet version (`G3_VERSION`) must agree. A v1 display shows NO SIGNAL with a v2 hub, and the reverse is also true.

### Hub wiring (ESP32-S3 N16R8)

Documented in `gauges/firmware/gauge3_v1/README.md`. Short form:

| Function | Hub GPIO | Goes to |
|---|---|---|
| CAN TX | 4 | SN65HVD230 CTX (D) — this build transmits Mode 01, so do not tie CTX to 3.3 V |
| CAN RX | 5 | SN65HVD230 CRX (R) |
| CAN-H / CAN-L / GND | — | OBD pins 6, 14, 5 |
| MAX31855 SCK / SO / CS | 12 / 13 / 10 | K-type probe on T+ / T− (cylinder 6, pre-turbo). Swap T+ and T− if the reading falls when the tip is heated |
| EC11 A / B / SW | 16 / 17 / 18 | Encoder common to GND |
| 5 V power | 5V | MP1584EN 5 V output, fed from an ignition-switched 12 V fuse tap (1 A) |

Displays need only 5 V and GND (USB-C or the power header). No data wires; the link is radio.

### EGT probe and amplifier

The cluster firmware is written for a **MAX31855** (3.3 V, SPI, K-type). A MAX31856 may already be on hand from the older 1.28 in prototype; that chip needs a different driver and is **not** drop-in. Confirm which board you have.

The probe is a K-type pyrometer in the exhaust manifold at cylinder 6 (rear, near the firewall — typically the hottest cylinder on this engine). Analog wiring from the older prototype (MAX31856 + 1.28 in GC9A01 + ESP32-C3) remains in `gauges/README.md` for reference and is superseded by the hub-and-display design.

Hub power: 12 V ignition → 1 A add-a-circuit mini fuse tap → MP1584EN fixed 5 V buck (max input about 23 V) → hub 5 V. Ground with a 22–18 AWG #10 ring terminal to chassis. All grounds common. The three displays can share the same 5 V rail; budget about 1 A combined and measure on the bench first.

### Arduino setup for the gauges

Same ESP32 core **3.3.12**.

```bash
arduino-cli core install esp32:esp32@3.3.12
arduino-cli lib install "Adafruit MAX31855 library"   # 1.4.2 was used; pulls Adafruit BusIO
```

No LVGL. Board settings for both the hub and each display:

- Arduino IDE: Board **ESP32S3 Dev Module**, Flash Size **16 MB**, PSRAM **OPI PSRAM**, Partition **16M Flash (3MB APP/9.9MB FATFS)**, USB CDC On Boot **Enabled**, USB Mode **Hardware CDC and JTAG**.

FQBN:

```
esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc
```

Upload `gauges/firmware/gauge3_v1/hub` once and `gauges/firmware/gauge3_v1/display` three times. Do **not** flash a merged 16 MB image — that erases NVS, which stores the display role and settings.

Set each display role over serial at 115200: `role 1` (EGT), `role 2` (boost / exhaust brake), `role 3` (rail / utility). Confirm with `info`. Label the back of each board.

Bench test without the truck: `sweep on` on the hub. Details, knob map, and starting alarm values are in `gauges/firmware/gauge3_v1/README.md`.

### Mounting notes

- The three 2.1 in displays are meant for a custom A-pillar pod (75 mm round faces). That CAD is not in this repo yet; see `gauge-pod/README.md`.
- Until the pod exists, the displays can sit on a bench or a temporary mount. Keep wiring clear of pedals and the steering column.
- Do not block curtain airbags or remove a factory A-pillar in a way that fights the airbag path. Commercial pod vendors warn about this; read the manufacturer pages before you cut or drill a pillar.
- The older 1.28 in single-gauge notes (LDR window, buzzer port, heat on a sun-loaded pillar) still apply to any in-cab enclosure: prefer PETG or ASA, and keep buzzers within their temperature rating.

## 8. Printing

Print files live in [`sniffer-case/`](sniffer-case/):

| File | Use |
|---|---|
| `exports/can_case_v01_base.stl` | Base, print orientation (floor down) |
| `exports/can_case_v01_lid.stl` | Lid, print orientation (outer face down) |
| `exports/can_case_v01_base.step`, `can_case_v01_lid.step` | CAD / remix |
| `src/can_case_v01.py` | CadQuery model, parameters at the top |

A geometry-only 3MF (both parts on a 256 mm plate) is produced by `src/export_v01.py`. If that binary is not in the tree you cloned, slice the two STLs or re-export from the CadQuery script.

**Orientation (from the v01 export):** base floor-down, lid outer face down. Outer size 50 × 35 × 20 mm (base 17 mm + lid 3 mm), 2.0 mm walls and floor. Four M2 heat-set inserts in the corner bosses; lid uses M2 × 6 socket caps.

**Material:** default **PLA** for a first fit-check on the bench. For a case that lives in the cab, **PETG or ASA is a better choice**. A parked truck in summer sun can take the dash and the area around the OBD-II port well past PLA’s glass-transition range (about 55–60 °C). PETG holds shape closer to 80 °C; ASA adds UV resistance and a higher heat limit. This is a starting recommendation, not a measured cabin-temperature study.

**Starting slicer settings** (not tuned on a specific printer; adjust to yours):

- 0.4 mm nozzle, 0.2 mm layer height
- 3 perimeters, 15–20 % infill, no supports expected in the official orientation
- Pause or use a soldering iron to seat the M2 inserts after the print
- Check the USB-C stadium opening against your cable overmold (the opening is an educated guess at 12 × 7 mm)

Open items in v01: the XIAO has no mechanical hold-down (cable plus a dab of hot glue), and the three-wire exit has no strain relief (tie a knot inside).

The A-pillar gauge pod is a separate print and is not published yet.
