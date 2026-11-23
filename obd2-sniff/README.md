# OBD2 Sniff-Everything (side project)

Truck: 2017 Ram 3500, 6.7 Cummins. Goal: see everything the OBD2 port carries. The results also feed the A-pillar gauge project (`truck-egt`), for example the truck's own "it's dark" and dash dimmer signals.

**Everything here is LISTEN-ONLY. Nothing in this project transmits, ACKs, or sends error frames on the truck's CAN buses.**

Status (Nov 21, 2026): firmware written and **compiled** on the box (Arduino-ESP32 core 3.3.12). **Not yet tested on hardware or on the truck.**

## Files

| Path | What it is |
|---|---|
| `docs/OBD2_pin_ID_guide.pdf` | Printable guide (black ink): find what every OBD2 pin is, with a multimeter only |
| `docs/OBD2_message_ID_log.pdf` | Printable blank log for message IDs found later (kept separate from the meter guide) |
| `docs/make_pdf.py` | Makes both PDFs (reportlab) |
| `firmware/sniffer_node/` | Node firmware: ESP32-S3 + SN65HVD230, one per CAN pair |
| `firmware/gateway/` | Gateway firmware: ESP32-S3 on the laptop USB, talks GVRET to SavvyCAN |
| `firmware/common/` | Shared headers (`sniff_proto.h`, `gvret_port.h`). Run `tools/sync_common.sh` after editing |
| `firmware/bins/` | Compiled binaries: `node_N16R8`, `node_N8R2`, `node_FAKE_N16R8`, `gateway_any_S3` |
| `tools/gvret_check.py` | Checks the gateway output without SavvyCAN (frames/s per bus) |
| `tools/dump_capture.py` | Saves a node's PSRAM recording to a SavvyCAN CSV |
| `labels.csv` | SUPVAN heat-shrink labels, 2 of each (one for each wire end) |

## How it fits together

```
 OBD2 port --> LINXINO breakout box
   pins 6/14 --> SN65HVD230 --> ESP32-S3 node 0 --\
   pins 3/11 --> SN65HVD230 --> ESP32-S3 node 1 ---+-- ESP-NOW, Wi-Fi channel 6 --> ESP32-S3 gateway --USB--> laptop: SavvyCAN (GVRET)
   other pair -> SN65HVD230 --> ESP32-S3 node 2 --/
   pin 4/5 ground --> every node GND
```

* Node N appears in SavvyCAN as **bus N** (0 to 4). **Bus 5** carries gateway status frames (ignore them when decoding).
* One firmware for every node. Set the node ID over USB serial, it is saved in flash.
* The gateway's LED and status table show frames/s, lost frames, and last-seen for each node.

## Step 0: find the pins (do this first)

Print `docs/OBD2_pin_ID_guide.pdf` and do it with the multimeter. Do not connect any node until you know which pins are CAN pairs.

Standard OBD2 (SAE J1962) pins: 4 chassis ground, 5 signal ground, 6 CAN high, 14 CAN low, 7 K-line, 15 L-line, 2/10 J1850, 16 battery +. Pins 1, 3, 8, 9, 11, 12, 13 are maker use.

FCA guesses (**unverified on this truck**): 3/11 = CAN-IHS (body bus, 125 kbit/s) and 6/14 = CAN-C (500 kbit/s).

**Educated guess, important:** on many 2011+ FCA trucks, the CAN-C on pins 6/14 is a *diagnostic* CAN that passes through the central gateway. It may be nearly silent until a scan tool asks for data. If node 0 shows few or no frames with the engine running, that is the likely reason, not a fault. Pins 3/11 may carry much more broadcast traffic.

## Wiring one node

| SN65HVD230 module pin | Connect to |
|---|---|
| 3V3 | ESP32-S3 **3V3** (not 5V) |
| GND | ESP32-S3 GND **and** OBD pin 4 or 5 (common ground is required) |
| CTX (TX / D) | **3V3** (recommended: hardware listen-only, the chip cannot drive the bus) or GPIO4 |
| CRX (RX / R) | ESP32-S3 **GPIO5** |
| CANH | CAN-high pin of the pair (example: OBD 6 or 3) |
| CANL | CAN-low pin of the pair (example: OBD 14 or 11) |

**Before connecting to the truck: remove or cut the 120 ohm resistor on each SN65HVD230 module** (usually a small SMD part marked `121` across CANH/CANL). The truck bus is already terminated (about 60 ohm). An extra 120 ohm drops it to 40 ohm. Check with the meter: module CANH to CANL should read open (OL) after removal.

Node power, pick one:
1. USB power bank or laptop USB into the node's USB port (simplest for bench and first truck test).
2. From OBD pin 16 through an **inline fuse (1 A) first**, then a 12 V to 5 V buck converter, then the node 5V pin. Pin 16 is battery power, always on; treat it as unfused. Ground the buck to pin 4/5.

Pin map (all nodes):

| Function | ESP32-S3 GPIO |
|---|---|
| TWAI TX (to CTX, or leave unused if CTX tied to 3V3) | 4 |
| TWAI RX (from CRX) | 5 |
| Onboard RGB LED (driven white only) | 48 (some boards 38: change `PIN_LED`) |
| BOOT button (start/stop PSRAM recording) | 0 |

Avoid GPIO 35, 36, 37 on the N16R8 (octal PSRAM uses them).

## Flashing

Arduino IDE 2: Board "ESP32S3 Dev Module", **USB CDC On Boot: Enabled**.
* N16R8 node: Flash Size 16MB, PSRAM "OPI PSRAM".
* N8R2 node: Flash Size 8MB, PSRAM "QSPI PSRAM".
* Gateway: any of the boards (an N8R2 is fine), defaults otherwise.

arduino-cli (what was used on the box):
```
arduino-cli compile -b esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M firmware/sniffer_node
arduino-cli compile -b esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=enabled,FlashSize=8M firmware/sniffer_node
arduino-cli compile -b esp32:esp32:esp32s3:CDCOnBoot=cdc firmware/gateway
```
arduino-cli may leave a `build/` folder inside the sketch folder. Delete it (binaries live in `firmware/bins/`).

Prebuilt: flash `firmware/bins/<variant>/*.merged.bin` at address 0x0 (esptool or the Espressif web flasher).

Set each node over its USB serial (Arduino Serial Monitor, newline on, any baud on the native USB port; 1,000,000 on the COM port):

| Command | Does |
|---|---|
| `n0` .. `n4` | node ID = SavvyCAN bus number (saves, reboots) |
| `b0` | auto speed (default) |
| `b500` `b250` `b125` `b83` `b33` | manual speed override (saves) |
| `f1` / `f0` | fake-bus bench mode on / off (saves, reboots) |
| `r3000` | fake mode: add 3000 filler frames/s (stress test) |
| `c` | start / stop PSRAM recording (same as BOOT button) |
| `d` | dump recording as SavvyCAN CSV (use `tools/dump_capture.py`) |
| `s` | stats   `v` print every frame   `h` help |

Auto speed: the node tries 500k, 250k, 125k, 83.3k, 33.3k, all in listen-only mode. It accepts a speed when frames arrive with no bus errors (or at least 30 frames with under 2% errors). It keeps cycling while the bus is asleep. In auto mode it re-detects if it sees errors and no frames for 4 s.

## LED codes (blink COUNT, white only, never color)

Node, repeats every ~2.5 s:

| Blinks | Meaning |
|---|---|
| 1 | Looking for the bus speed / bus silent |
| 2 | Bus OK, gateway not heard |
| 3 | Streaming OK |
| 4 | Frames dropped in the last 5 s |
| 5 | Bus errors (wrong speed or wiring) |
| 6 | Fake-bus mode, streaming |
| Long 1 s on / 1 s off | Recording to PSRAM |

Gateway: 1 = no nodes heard, 2 = nodes heard but SavvyCAN not connected, 3 = streaming with no drops, 4 = drops in the last 5 s.

## Bench test (no truck)

1. Flash the gateway. Plug its **USB** (native) port into the laptop.
2. Flash 1 to 3 nodes. On each: `n0`, `n1`, `n2`, then `f1` (fake mode). Power them from any USB charger.
3. Gateway serial monitor shows a status table every 2 s: fps, frames, lost (%), packet loss, last seen. Node LEDs show 6 blinks.
4. Stress: on one node type `r3000` (about 4,000 frames/s total, like a busy 500k bus). Watch the lost % on the gateway.
5. Close the serial monitor and run `python tools/gvret_check.py COM7` or connect SavvyCAN.

Fake data includes ID 0x2A0 (byte 0 bit 0 toggles every 10 s like headlights, byte 1 ramps like a dimmer), 0x0F0 (RPM-like ramp), 0x2B0 (a "door" bit every 15 s), and about 40 IDs with counters. Good practice for the SavvyCAN sniffer view.

## SavvyCAN setup

1. Install SavvyCAN (free, savvycan.com). Close any serial monitor on the gateway port.
2. Connection > Open Connection Window > Add New Device Connection > **Serial Connection (GVRET)** > pick the gateway's port > Create.
3. The gateway reports 6 buses. Bus 0..4 = nodes, bus 5 = gateway status (ID 0x1F5C000n: bytes 0-1 fps, 2-3 lost frames, 4 node state, 5 last-seen x100 ms, 6-7 speed kbit/s).
4. You do not need to set bus speeds in SavvyCAN; the nodes set their own. SavvyCAN "send" is ignored by design.
5. Useful views: **RE Tools > Sniffer** (bytes that change are highlighted; set "fade" so steady bytes go quiet), **RE Tools > Flow View** (one ID's bytes over time), **Frame info** (per ID frequency, which bits change), **File > Save Log File** (GVRET native CSV).

The gateway stays in GVRET mode until reset. Press its RESET button to get the text table back.

## Capture-session script (after the pins are known)

Truck in PARK, parking brake on, outside. One person acts, one types markers. Before each action, add a marker: in SavvyCAN, note the time stamp; or type the marker into a text file with the clock time. Wait about 10 s between actions, and do each action 3 times (on, off, on) so the changing bits are easy to find.

1. Key OFF, doors shut, 1 minute (sleep traffic). Save as `01_key_off.csv`.
2. Key ON, engine OFF, 1 minute. `02_key_on.csv`
3. Engine idle, 1 minute with no actions. `03_idle_baseline.csv`
4. One action at a time, about 10 s apart, 3 times each, in its own log if you can:
   headlights on/off; switch to AUTO headlights and cover the dash light sensor; dash dimmer full up, full down, slowly; each door open/close; hood; turn signal left, right, hazards; brake pedal; gear P R N D (foot on brake); A/C on/off, fan speeds; exhaust brake on/off; cruise on, set, resume, cancel.
5. Short drive (5 minutes), with a passenger typing markers for stops, turns, exhaust brake use.

Name each file with the action. Decode the CSV files plus marker notes into an ID table.

Finding bytes yourself: open a log, RE Tools > Sniffer or Frame Info, filter to one ID, and look for a byte that changes only when you did the action. Then use **Flow View** on that byte to see it step with the action.

## Throughput: what to expect (honest)

* A 500 kbit/s bus at 100% load carries about 4,000 to 4,500 frames/s (8-byte standard frames). A busy truck bus is often 30 to 60% load, so about 1,500 to 3,000 frames/s (educated guess).
* Node packing: up to ~97 frames per 1470-byte ESP-NOW v2 packet; packets go out every 4 ms or when full. At 4,000 frames/s that is about 250 small packets/s, about 70 KB/s per node.
* ESP-NOW is set to 24 Mbit/s air rate to the gateway (unicast with retries). Real-world ESP-NOW throughput at this rate should be a few hundred KB/s in a quiet cab (**educated guess, not measured**). Two busy buses should fit. Five very busy buses might not.
* Gateway to laptop: native USB handles this easily. The COM/UART port at 1,000,000 baud is about 100 KB/s, so about 5,000 frames/s total; use the native USB port.
* Buffers: TWAI queue 512 frames + node queue 3,072 frames, roughly 0.8 s of burst at 4,000 frames/s.
* Time stamps are software time stamps (taken when the frame is read), about 0.1 to 1 ms jitter. Cross-node alignment is about 1 to 3 ms (educated guesses). Good enough to match actions to bytes, not for bit-timing work.

**Loss is measured, not guessed:** each packet carries the running frame number, so the gateway counts lost frames exactly (status table and bus 5).

Fallbacks if a node shows loss:
1. Move the gateway closer, change `SNIFF_WIFI_CHANNEL` in `firmware/common/sniff_proto.h` (1, 6 or 11; reflash all boards), or run fewer nodes at once.
2. **Plug the busiest node straight into the laptop.** Every node also speaks GVRET on its own USB. In SavvyCAN, add it as a second GVRET connection. No reflash needed.
3. **PSRAM recording:** press BOOT on the node to start and stop. N16R8: about 400,000 frames (about 100 to 250 s on a busy bus). N8R2: about 100,000 frames. Then plug in the node and run `python tools/dump_capture.py COM7 file.csv`. Use an N16R8 for the busiest bus.

## Things to check (unverified)

* Which pins are live on this truck (the PDF guide).
* Whether pins 6/14 carry broadcast traffic or only diagnostic replies (see above).
* The 120 ohm resistor is removed from every SN65HVD230 module.
* The onboard LED pin (48 or 38) on each ESP32-S3 board.
* 83.3k and 33.3k timing values are calculated (80 MHz / brp 48 or 120 / 20 quanta), not tested on a real bus.
* FCA security gateway: believed to start on later models; the 2017 should be before it (unverified).
