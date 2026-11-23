# gauge3_v1 - 3-gauge A-pillar pod, starter firmware

Truck: 2017 Ram 3500, 6.7 Cummins, pre-SGW. Status: compiles. NOT yet tested on hardware.

**v2 (Nov 22 2026), matched to the v4 drive log** (`../../obd2-sniff/decode/README.md`, section "v4 drive"): hub 1.1 / display 1.1, packet `G3_VERSION 2`. **Reflash the hub AND all 3 displays** (a v1 display shows NO SIGNAL with a v2 hub). Gauge 2 = BOOST 0-40 psi / EXH BRAKE (real EBP, PID 73) 0-80 psi with BRAKE HP. Gauge 3 VGT VANES page removed; RAIL LAG (> 2,500 psi for > 1 s) and new RAIL LOW (< 4,000 psi, engine running) alarms. The hub now also listens to the CAN-C broadcast 0x30A (byte 7 = exhaust-brake effort). New renders: `previews_v2/`.

This folder replaces the older files in the parent folder (`../egt_gauge*`, `../render_host`, `../../README.md` wiring).
Those older files use the MAX31856 amplifier and the GC9A01 1.28 in display. They are superseded. Do not mix them with this folder.

---

## 1. Safety

**WARNING:** Do not connect the hub to the OBD-II port while a tuner or scan tool writes (flashes) the ECM. A second device on the bus during a flash can damage the ECM. Disconnect the hub first.

**WARNING:** Do not look at the gauges for long periods while you drive. Set the alarm buzzer so that you can hear it. Do the bench tests and the PID probe with the truck parked.

**WARNING:** OBD-II pin 16 is battery +12 V at all times. Use the ignition-switched add-a-circuit fuse tap (1 A fuse) for power. A permanent connection will discharge the battery.

**CAUTION:** The SN65HVD230, the MAX31855 and the EC11 use 3.3 V only. Do not connect 5 V or 12 V to them.

**CAUTION:** Many SN65HVD230 boards have a 120 ohm resistor between CAN-H and CAN-L. The truck bus already has its terminators. Remove or cut that resistor before you connect to the truck. Measure about 60 ohm between OBD pins 6 and 14 with the key OFF and the hub disconnected. Measure again with the hub connected. The value must not drop to about 40 ohm.

**CAUTION:** The alarm values in this firmware are editable STARTING VALUES. They are not truck specifications. Set your own values before you rely on them.

**CAUTION:** The MP1584EN buck has a maximum input of about 23 V. The hub and 3 displays can take about 1 A at 5 V. Measure the current on the bench before you install.

---

## 2. Contents

| Path | Content |
|---|---|
| `hub/hub.ino` | Under-dash hub: CAN (OBD-II), EGT, knob, alarms, settings, bench test mode, ESP-NOW sender |
| `hub/obd_pids.h` | All PID definitions in one table. TODO for Cummins Mode 22 (empty on purpose) |
| `hub/obd.cpp`, `obd.h` | TWAI CAN driver, ISO-TP (multi-frame) receive, poller, `probe` report |
| `hub/knob.cpp`, `knob.h` | EC11 decoder and debounce |
| `display/display.ino` | One sketch for all 3 displays. Role 1, 2 or 3 |
| `display/ui.cpp`, `gfx.cpp`, `fonts.cpp` | Screen drawing (pure C++, also runs on a PC for previews) |
| `display/Display_ST7701.*`, `TCA9554PWR.*`, `I2C_Driver.*`, `Gyro_QMI8658.*` | Waveshare official demo drivers (see section 9) |
| `hub/g3_packet.h`, `display/g3_packet.h` | Shared ESP-NOW packet. The two copies must be identical (`tools/check_shared.sh`) |
| `firmware_bins/` | Compiled app binaries (bootloader, partitions, app) |
| `previews_v2/` | v2 PNG screens rendered from the real `ui.cpp` code, with Nov 22 drive values. `00_contact_sheet.png` shows all |
| `previews/` | v1 renders (old ranges, VGT page). Kept for comparison |
| `tools/` | `build_all.sh`, `make_fonts.py`, preview renderer |
| `vendor_ref/` | Waveshare schematic and the original vendor display files |

---

## 3. Hub wiring

Board: ESP32-S3-DevKitC type, ESP32-S3-WROOM-1 **N16R8**.
These GPIO are NOT used: 26-32 (flash), 33-37 (OPI PSRAM), 19/20 (USB), 0/3/45/46 (strapping), 43/44 (UART0).

| Function | Hub pin | Connect to | Note |
|---|---|---|---|
| CAN TX | GPIO4 | SN65HVD230 CTX (D) | |
| CAN RX | GPIO5 | SN65HVD230 CRX (R) | |
| CAN power | 3V3, GND | SN65HVD230 VCC, GND | 3.3 V only |
| CAN-H | - | OBD-II pin 6 | twisted pair with CAN-L |
| CAN-L | - | OBD-II pin 14 | |
| Signal ground | GND | OBD-II pin 5 (and pin 4) | common ground |
| EGT clock | GPIO12 | MAX31855 SCK (CLK) | |
| EGT data | GPIO13 | MAX31855 SO (DO) | |
| EGT select | GPIO10 | MAX31855 CS | |
| EGT power | 3V3, GND | MAX31855 VIN, GND | |
| Thermocouple | - | MAX31855 T+ / T- | K-type, cyl-6 pre-turbo. If the value falls when the tip gets hot, swap T+ and T- |
| Knob A | GPIO16 | EC11 A (CLK) | |
| Knob B | GPIO17 | EC11 B (DT) | |
| Knob common | GND | EC11 C (middle pin) | |
| Knob switch | GPIO18 | EC11 SW | other switch pin to GND |
| Power in | 5V | MP1584EN 5 V out | input = ignition 12 V, 1 A fuse tap |

Knob noise: the firmware uses internal pull-ups and software debounce. For a long cable, add 10 k pull-up to 3V3 and 10 nF to GND on A and B.

Displays: each Waveshare board gets 5 V and GND (USB-C or its power header). No data wires. The link is ESP-NOW (radio).

---

## 4. Software set-up (one time)

1. Install arduino-cli (install the official Arduino CLI).
2. Install the core: `arduino-cli core install esp32:esp32@3.3.12`.
3. Install the library: `arduino-cli lib install "Adafruit MAX31855 library"` (1.4.2 used). It also needs `Adafruit BusIO`.
4. No LVGL is necessary. The display draws with its own code.

Board settings (FQBN), same for both boards:

```
esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc
```

In the Arduino IDE: Board "ESP32S3 Dev Module", Flash Size 16MB, PSRAM "OPI PSRAM", Partition "16M Flash (3MB APP/9.9MB FATFS)", USB CDC On Boot "Enabled", USB Mode "Hardware CDC and JTAG".

---

## 5. Flash the boards

**CAUTION:** Use the commands below. Do not write a full "merged" 16 MB image. A merged image erases NVS, and NVS keeps the gauge role and your settings.

### 5.1 Hub

1. Connect the hub USB port to the PC.
2. Find the port: `arduino-cli board list`.
3. Compile: `arduino-cli compile --fqbn "<FQBN>" hub`
4. Upload: `arduino-cli upload --fqbn "<FQBN>" -p <PORT> hub`
5. If the upload does not start, hold BOOT, push RESET, release BOOT. Do step 4 again.
6. Open the monitor: `arduino-cli monitor -p <PORT> -c baudrate=115200`.
7. Type `help`. The command list must show.

The hub console works on both USB ports of the DevKitC (native "USB" and "UART/COM").

### 5.2 Displays (do this 3 times)

1. Connect one display to the PC with USB-C.
2. Compile: `arduino-cli compile --fqbn "<FQBN>" display`
3. Upload: `arduino-cli upload --fqbn "<FQBN>" -p <PORT> display`
4. If the upload does not start, hold BOOT, push RESET, release BOOT. Do step 3 again.
5. Set the gauge role (section 6).
6. Put a label on the back of the board: "1 EGT", "2 BOOST/EXH BRAKE" or "3 UTILITY".

Pre-built binaries are in `firmware_bins/`. Write addresses: bootloader `0x0`, partitions `0x8000`, boot_app0 `0xe000`, app `0x10000`.

---

## 6. Set the gauge role

Role 1 = EGT. Role 2 = BOOST / EXH BRAKE. Role 3 = UTILITY.

1. Connect the display to the PC with USB-C.
2. Open the monitor at 115200.
3. Type `role 1` (or `role 2`, `role 3`) and push Enter.
4. Make sure that the monitor shows `role = n (saved)`.
5. Type `info`. Make sure that the role is correct.

The role is kept in NVS. A new upload keeps it.
Alternative: compile with `--build-property "compiler.cpp.extra_flags=-DGAUGE_ROLE_DEFAULT=2"`. This default is used only when NVS has no role yet.

Other display commands: `help`, `info`, `demo on` / `demo off` (shows the preview screens without the hub), `buzz`, `imu`.

---

## 7. Knob use (hub EC11)

| Action | Nothing selected | One gauge selected | All 3 selected |
|---|---|---|---|
| Turn | Dim / brighten all 3 (5 % steps) | Change page: MAIN -> PEAK -> SETTINGS. Gauge 3 MAIN: utility pages, then PEAK, then SETTINGS | MAIN <-> PEAK on all 3 |
| Click | Select EGT | Select next: EGT -> GAUGE 2 -> GAUGE 3 -> ALL -> none | Select none |
| Hold 1.5 s | Reset all peaks | Reset peaks of that gauge. Gauge 3 PITCH/ROLL page: set level (zero) | Reset all peaks |
| Any click while the buzzer sounds | Mute. No other change | Mute. No other change | Mute. No other change |

Selected gauge = bright white outer ring. Selection clears after 8 s with no knob input.

On the SETTINGS page:
1. Turn to move to an item.
2. Click to edit. The item becomes white.
3. Turn to change the value.
4. Click to save. Hold to cancel.
5. Turn past the last item to go back to MAIN. Turn before the first item to go to PEAK.

---

## 8. Bench test (USB only, no truck)

You do not need the truck or a CAN simulator.

1. Supply 13.8 to 14.4 V from the bench supply to the MP1584EN input. Or use USB power.
2. Turn on the hub and the 3 displays.
3. Make sure that each display shows "NO CAN" at the top within 2 s. "NO SIGNAL" means the display gets no packets.
4. Open the hub monitor. Type `sweep on`.
5. Watch a 30 s cycle: idle, tow up a grade (EGT goes past ALARM, rail pressure lags), exhaust brake (gauge 2 changes to EXH BRAKE, about 58 psi and 80 HP), idle, low battery.
6. Make sure that the EGT buzzer sounds at the EGT alarm. Click the knob. The buzzer must stop. The screen must not change.
7. Type `sweep off`.
8. Set single values. Examples:
   - `set egt 1350`
   - `set boost 32`
   - `set ebp 58` then `set brake_hp 86` (EXH BRAKE after the SWITCH DELAY; `set brake_hp 0` goes back to BOOST)
   - `set frp_cmd 21500` then `set frp_act 18000` (RAIL LAG after 1 s)
   - `set rpm 700` then `set frp_act 3600` (RAIL LOW after 1 s)
   - `set load 80`, `set pedal 85`, `set iat 120`, `set batt 11.5`, `set rpm 2000`
9. Type `clear egt` to remove one value. Type `clear all` to remove all.
10. Type `status` to see all values, their source and the alarm bits. Type `watch on` for 2 lines per second.
11. Real EGT: connect a K-type probe to the MAX31855. Heat the tip with a lighter or hot water. Compare with the PerfectPrime TC41. The real reading is used in sweep mode too. Only `set egt` replaces it.
12. Knob without the EC11: type `click`, `hold`, `left`, `right`.
13. Settings from USB: `settings` (list), `setting 1 1450` (example: EGT ALARM), `defaults`.

Starting values (editable, NOT truck specs):

| # | Setting | Start | Gauge |
|---|---|---|---|
| 0 | EGT WARN | 1250 F | 1 |
| 1 | EGT ALARM | 1400 F | 1 |
| 2 | BOOST WARN | 35 psi | 2 |
| 3 | BOOST ALARM | 38 psi | 2 |
| 4 | EBP WARN | 65 psi | 2 |
| 5 | EBP ALARM | 70 psi | 2 |
| 6 | SWITCH DELAY (BOOST -> EXH BRAKE) | 1.0 s | 2 |
| 7 | RAIL LAG (actual below commanded, > 1 s) | 2500 psi | 3 |
| 8 | RAIL LOW (actual below this, engine > 400 rpm, > 1 s) | 4000 psi | 3 |
| 9 | BATT LOW (for 3 s) | 12.0 V | 3 |
| 10 | IAT WARN | 150 F | 3 |

Where the v2 numbers come from (Nov 22 drive): boost max 32.3 psi at 82 % pedal; EBP 18-51 psi on pulls, 55-60 psi on the exhaust brake, peak 66; rail 4.3-25.7 kpsi, idle about 4,635 actual / 4,696 cmd, every lag over 2,500 psi lasted under 0.5 s. Boost 35/38 and EBP 65/70 are educated guesses / owner reports, not factory specs.

Gauge 2 logic (v2): BOOST (0-40 psi) shows under throttle; its second line shows EBP. EXH BRAKE (0-80 psi) shows when the exhaust brake is working (CAN-C 0x30A byte 7 > 0, pedal not above 10 %) for SWITCH DELAY. The big number is exhaust back pressure (PID 73), the second line is BRAKE HP (byte 7 x 1.32, same 0-230 number as the dash). Back to BOOST when byte 7 = 0 or pedal > 10 % for 0.3 s. If 0x30A is not seen, the v1 rule is used (pedal < 5 % AND (EBP >= 8 psi OR EBP 5 psi above boost)). If the truck has no EBP PID, gauge 2 stays on BOOST and shows "EBP N/A".

Gauge 3 pages (v2): RAIL PRESSURE (0-30K psi, actual = arc + number, commanded = white tick + CMD line, taller pale segments from 27K psi = red-zone marker), ENGINE LOAD (0-100 %, white tick + PEDAL line), INTAKE AIR, BATTERY, PITCH / ROLL, then PEAK and SETTINGS. 5 page dots.

Colour-blind design: no red/green. Blue = normal, amber = warn, white = alarm. Warn and alarm zones have taller arc segments. Text pills show WARN, HIGH, MUTED, RAIL LAG, RAIL LOW. Alarm = blinking white centre disc. EXH BRAKE mode = black text in a white box.

---

## 9. Truck PID probe

Do this one time before you trust any CAN value.

1. Park the truck. Set the parking brake.
2. Make sure that no tuner or scan tool is connected.
3. Connect the hub to the OBD-II port (pins 6, 14, 5).
4. Turn the key to ON. Start the engine if you want live values.
5. Connect a laptop to the hub USB port. Open the monitor at 115200.
6. Type `probe` and push Enter.
7. Wait about 5 s. The hub prints:
   - every module that answers (0x7E8 = ECM, 0x7E9 = TCM, and others),
   - the 6 supported-PID bitmaps (0x00, 0x20, 0x40, 0x60, 0x80, 0xA0),
   - one line per PID: YES / no, name, gauge value, raw data bytes,
   - the sources chosen for BOOST, RAIL ACTUAL and PEDAL,
   - EBP, VGT and RAIL CMD: "Mode 01 OK" or "needs Mode 22" (VGT is reported only; v2 does not show or poll it).
8. Copy the full report. Keep it with the project notes.
9. Type `status`. Make sure that each value is near the factory dash value (boost, IAT, battery).
10. If MAP comes from PID 0x0B, boost reads flat above about 22 psi. Tell the firmware author.

If a value "needs Mode 22", the gauge shows "--" or "N/A". Add the Cummins Mode 22 address in `hub/obd_pids.h` (MODE22_TABLE) only from a trusted source. The firmware does NOT guess Mode 22 addresses.

---

## 10. Where the display pins came from

Source: official Waveshare demo `ESP32-S3-Touch-LCD-2.1-Demo.zip`
(https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1/ESP32-S3-Touch-LCD-2.1-Demo.zip, linked from https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.1/Resources-And-Documents).

| Item | File in the zip |
|---|---|
| RGB pins, timings (16 MHz, HPW 8 / HBP 10 / HFP 50, VPW 3 / VBP 8 / VFP 8), backlight GPIO6, ST7701 SPI CLK GPIO2 / MOSI GPIO1 | `Arduino/examples/LVGL_Arduino/Display_ST7701.h` |
| ST7701 init sequence, CS = EXIO3, reset = EXIO1 | `Arduino/examples/LVGL_Arduino/Display_ST7701.cpp` |
| TCA9554 at 0x20 | `Arduino/examples/LVGL_Arduino/TCA9554PWR.*` |
| I2C SDA GPIO15 / SCL GPIO7 | `Arduino/examples/LVGL_Arduino/I2C_Driver.h` |
| QMI8658 at 0x6B | `Arduino/examples/LVGL_Arduino/Gyro_QMI8658.*` |
| Buzzer = EXIO8, high = on | `ESP-IDF/ESP32-S3-Touch-LCD-2.1-Test/main/Buzzer/Buzzer.c` |

RGB data pins D0-D15: 5, 45, 48, 47, 21, 14, 13, 12, 11, 10, 9, 46, 3, 8, 18, 17. HSYNC 38, VSYNC 39, DE 40, PCLK 41.
Only change to the vendor files: no LVGL include, no touch include, `Touch_Init()` removed (touch DISABLED).

---

## 11. Items not verified yet

1. No code has run on real hardware. Only compile tests and PC renders.
2. ST7701 double-buffer swap with bounce buffers: expected to work (ESP-IDF RGB panel feature). Check for tearing or flicker.
3. QMI8658 axis directions and signs for pitch/roll in the pod. Use `imu` and `IMU_PITCH_SIGN` / `IMU_ROLL_SIGN` in `display.ino`.
4. Done Nov 22 2026 with the C6 probe: 0x6D, 0x73, 0x87 answer on this ECM; 87 = 1/32 kPa confirmed, 73 x0.01 kPa plausible (no mechanical EBP gauge to compare). Hub v2 accept-all CAN filter + 0x30A listening: not run on the truck yet. Original note: PID support on this ECM: run `probe` (section 9). Byte layouts of 0x6D, 0x70, 0x71, 0x73, 0x87 follow SAE J1979-DA as published. Check raw bytes.
5. EC11 steps per detent. If one click moves 2 steps, or 2 clicks move 1 step, change `KNOB_STEPS_PER_DETENT` in `hub/knob.h`.
6. Amplifier: this firmware uses MAX31855. `../../PARTS.csv` lists a MAX31856 on order. Confirm which board you have. The MAX31856 needs a different driver.
7. ESP-NOW range in the cab with the hub under the dash.
