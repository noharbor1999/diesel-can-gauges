# Truck EGT gauge (added Nov 15, 2026)
Goal: show EGT from the existing pyro in the 6.7 Cummins exhaust; tuner broke, tune still loaded.
Stock candidates: MAX6675 boards (x6 from two Ferwooh sets), ESP32-C3 mini, XIAO ESP32-C6, 0.96in SSD1306 OLED, 1.28in Waveshare round LCD, LM2596 buck, TC41 meter for cross-check.
Open: probe type/junction, connector, mount location, warning threshold, power source (ignition-switched fuse tap).

## Decisions (Nov 15, 2026)
- Amp: MAX31856 (to ~2460 F); EGT has been seen near 2300 F on hard bursts. MAX6675 too low (1875 F max).
- Display: Waveshare 1.28in round 240x240 LCD (in stock), A-pillar mount. Mount via 3D scan of pillar/grab-handle bolts later (scanner on order); CAD owns mount.
- Background colors: green normal, yellow/orange hot, red trouble, blinking red danger. Pair with text label (designed for red-green color blindness).
- Thresholds: pending owner confirmation.
- Probe location: pre-turbo, exhaust manifold at cylinder 6 (rear, by firewall; hottest cylinder).
- Proposed bands (educated guess, awaiting OK): green <1200 F OK, yellow/orange 1200-1400 HOT, red 1400-1600 HIGH, blink red >1600 DANGER.
- Nov 15: Bands APPROVED as starting point (editable constants). No word labels; pure primary colors only (green 00FF00, yellow FFFF00, red FF0000, blink red/black).

## Firmware v1 (Nov 15, 2026) - firmware/egt_gauge/egt_gauge.ino
Compiles for ESP32-C3 (esp32 core 3.3.12): 342 KB flash, 15 KB RAM. Libs: Adafruit GC9A01A, Adafruit MAX31856, Adafruit GFX.
Shows big EGT number, peak, band background; NO PROBE on black on open/fault. 5 reads/s, light smoothing. Thresholds at top of file.
Wiring: SCK4 MOSI6 MISO5 shared; LCD CS7 DC10 RST1 BL->3V3 VCC->3V3; MAX31856 CS0 VIN->3V3; 12V ign fused 1A -> LM2596 set 5.0V -> C3 5V; common GND.

## v2 (Nov 15, 2026) - compiles: 357 KB flash, 19 KB RAM. v1 kept at firmware/egt_gauge_v1_bak-20261115
- EC11 encoder w/ push (inventory rows 279/280): A GPIO20, B GPIO21, SW GPIO3, C to GND, internal pullups.
- MAIN -click-> MENU (HISTORY/SETTINGS/BACK). HISTORY: turn 5/10/15/30 min, click back. SETTINGS: HOT/HIGH/DANGER edit +/-25 F, saved to flash (NVS); RESET PEAK. 30 s idle -> MAIN; EGT >= DANGER forces MAIN.
- History: 1 sample/s, 30 min RAM ring buffer (cleared at key-off).
- Voltmeter: bayite 3-wire 0-30V 0.36in red (row 307), standalone; red+yellow to fused ignition 12 V, black GND. Housing by CAD with LCD + encoder (A-pillar, after 3D scan).

## v4-v5 (Nov 15, 2026)
- v4: main screen = 24-segment ring (100 F each, labels 0-24 x100 F) for live EGT; peak segment stays lit + wedge; center number = PEAK only. The preview was approved (main_screen_preview_v4.png).
- v5: no menu. Turn flips MAIN/HISTORY/SETTINGS (wraps; DETENTS_PER_SCREEN). MAIN click = reset peak. HISTORY click = enter, turn 5/10/15/30/BACK, click = done. SETTINGS click = enter, HOT/HIGH/DANGER/BACK, click edit, +/-25 F, click save. Page dots on HISTORY/SETTINGS. Backups egt_gauge_v2/v3/v4_bak-20261115.
- v6: PK HOLD setting (default 30 s, 0=OFF, 10 s steps to 600 s, saved to flash): peak holds N s after last new high, then resets to live. Backup egt_gauge_v5_bak-20261115.ino.

## Decisions (CAD, Nov 15, 2026)
- Style: Banks iDash Stealth Pod look (sleek add-on cup, bolts to lower grab-handle hole, keeps factory pillar and handle). Ref photo: research/pods/featured_03_banks_63385_installed_with_handle.jpg
- Voltmeter dropped. Pod holds only the 1.28in round LCD + EC11 encoder.
- Still waiting on Moose Lite scan of A-pillar + handle bolt holes.

### Pod requirements added from firmware v7 (Nov 15, 2026)
- LDR window facing the cabin, baffled from the LCD's own glow (opaque wall between LCD bezel and LDR pocket).
- Sound port to the cabin for the MLT-9650/HN9650B buzzer (~9.6 mm dia; confirm height with calipers before CAD).
- Light-tight cover or pocket over the ESP32-C3 SuperMini onboard LED (GPIO8, glows with backlight PWM).
- Contents stay: Waveshare 1.28in GC9A01, EC11, SuperMini, buzzer driver. No voltmeter.
- Buzzer is rated to +70 C on a sun-loaded A-pillar: print in a heat-tolerant material (PETG+ minimum, ASA preferred) and keep the buzzer away from the windshield-facing skin. The buzzer's rating stays a risk regardless of the material.
- Still waiting on the scan before any geometry.

## Firmware v7 (Build, Nov 15, 2026) - firmware/egt_gauge/egt_gauge.ino
Compiles clean (zero errors, zero warnings with --warnings all), esp32 core 3.3.12, FQBN esp32:esp32:esp32c3:CDCOnBoot=cdc:
719,293 B flash (54% of 1,310,720), 25,096 B RAM globals (7%). BLE stack in this core build for C3 = NimBLE.
Backup of v6: firmware/egt_gauge_v6_bak-20261115.ino. Phone page: firmware/egt_phone.html.
Renders (from the real drawing code compiled on the PC against Adafruit GFX GFXcanvas16): firmware/renders/all_screens_v7.png + 32 PNGs.
Host build/tests: firmware/render_host/ (build.sh; `./render --test` = 60 logic checks pass; `./render --incr` = incremental redraw matches full redraw pixel-for-pixel; `./render out && python3 make_pngs.py` = renders).

### Kept from v4-v6
24-segment ring = live EGT (100 F/segment, labels 0-24 x100 F); peak segment stays lit + wedge; centre number = PEAK only; background pure green/yellow/red/blinking red at HOT/HIGH/DANGER (1200/1400/1600 F, editable); no-menu knob; MAIN click = reset peak; PK HOLD (30 s after the last new high); 30 s idle -> MAIN; DANGER forces MAIN; settings in NVS. Only pure primaries + black/white; no colour word labels.

### New in v7
- Carousel: MAIN > HISTORY > LOG > SETTINGS (wraps). 4 page dots.
- Cool-down cue (bottom gap of the ring): once EGT has been at/above HOT this drive and is back under HOT, MAIN shows COOL DOWN until EGT <= COOL AT, then OK TO SHUT OFF. Cue clears when EGT climbs 100 F above COOL AT (driving again). COOL AT default 300 F is an EDUCATED GUESS (no source); tune on the truck. (`COOL_CUE_WHILE_HOT = true` also shows COOL DOWN during the pull.)
- Buzzer: DANGER = beep-beep-pause (150/100/150/700 ms) for the whole DANGER event; one 60 ms chirp when entering HIGH (max 1 per 10 s). BUZZER ON/OFF setting (test beep when set ON). MAIN click resets peak AND mutes the current DANGER event; mute ends when the event ends (EGT < DANGER - 25 F). MAIN shows MUTED while muted.
- Hysteresis: colour bands and the alarm step down only 25 F under a threshold (stops flicker at a line).
- Backlight PWM (2 kHz, GPIO8): DAY BRT (100%), NIGHT BRT (30%), AUTO DIM (ON), DIM AT (20% light). Light sensor switches DAY/NIGHT after 4 s past the threshold (+5% hysteresis). Brightness edits preview live; DIM AT edit shows the live light %.
- Drive log (NVS): per power cycle: drive no., max EGT, run minutes, seconds above HIGH, seconds above DANGER, top 3 HIGH events (peak F + time above HIGH). Last 10 drives. Writes: the current drive at 60 s, every 3 min, and at the end of each HIGH event (about 20-25 small writes per hour); the list itself is written once per boot. Power-ups under 60 s are not logged. RUN time can be up to 3 min short (last save before key-off). LOG screen: browse = THIS DRIVE; click, turn = older drives, BACK. SETTINGS > CLEAR LOG asks NO/YES (NO first).
- Settings = scrolling list (selected row in the middle, arrows, n/12): HOT, HIGH, DANGER, PK HOLD, COOL AT, BUZZER, DAY BRT, NIGHT BRT, AUTO DIM, DIM AT, CLEAR LOG, BACK. An edit not clicked within 30 s is cancelled.
- Bluetooth LE "EGT Gauge": service d71c5700-b298-4fbf-911d-f4ed8b480e08. LIVE ...5701 (read/notify 2 Hz, 16 B: ver, band, flags, light%, EGT, peak, HOT, HIGH, DANGER, COOL). CTRL ...5702 (write [cmd][u16 offset]: 1 = history chunk of 240 samples, 2 = log). DATA ...5703 (read the answer). Notify is 16 B every 500 ms; answers are built in the BLE callback (<1 ms), so display and 5 Hz sampling are not held up.
- egt_phone.html: Chrome on Android, CONNECT, live round gauge (same colours/layout), status line, history 5/10/15/30 min with threshold lines (auto refresh 30 s), drive log table. Must be opened over https:// (e.g. GitHub Pages) or localhost; Web Bluetooth does not run on a plain http page. Add #demo to the URL for a preview without the gauge.

### Pin and strapping decision (Espressif ESP32-C3 Series Datasheet v2.5, section 3, Tables 3-1 and 3-3)
- GPIO2, GPIO8, GPIO9 are strapping pins. Defaults: GPIO2 floating, GPIO8 floating, GPIO9 weak pull-up.
- SPI (normal) boot needs GPIO9 = 1 and GPIO2 = 1 (note: GPIO2 "does not actually determine" the mode but Espressif recommends pulling it up because of glitches). GPIO8 = any value in SPI boot. Download boot = GPIO9 0, GPIO8 1.
- Waveshare 1.28in module schematic: BL -> TXB0108 -> 1k -> S8050 base with 10k base pull-up to 3V3. So BL can sit near 0.7 V (low) at reset. That is unsafe on GPIO9/GPIO2 but OK on GPIO8.
- A light sensor needs an ADC pin (GPIO0-4). The only free ADC pin was GPIO2, and an LDR divider pulls it low in light or dark. That is not clean, so: **LCD RST moves from GPIO1 to 3V3** (the library does a software reset when RST = -1). GPIO1 (ADC1_CH1, not a strap pin, works with BLE) becomes the light sensor.
- Final: GPIO1 = light sensor, GPIO8 = backlight PWM, GPIO2 = buzzer via a driver whose resistors pull GPIO2 HIGH at reset (buzzer silent at boot), GPIO9 = free (BOOT button).
- Light sensor chosen over 12 V headlight sense: no wire into the truck harness, no automotive transients, no PWM'd/DRL lamp feeds to filter, no optocoupler (none in stock). It also follows real ambient light (tunnels, garages).

### v7 wiring table
| Item | Connection |
|---|---|
| Power | 12 V ignition add-a-circuit mini fuse tap (1 A mini fuse) -> MP1584EN fixed 5 V mini buck IN+ (green, replaces LM2596) -> C3 5V. Ground via 22-18 AWG #10 ring to chassis. All grounds common. |
| SPI | SCK GPIO4, MISO GPIO5, MOSI GPIO6 (LCD + MAX31856 shared) |
| LCD (Waveshare GC9A01) | VCC 3V3, GND, DIN GPIO6, CLK GPIO4, CS GPIO7, DC GPIO10, **RST -> 3V3 (CHANGED from GPIO1)**, **BL -> GPIO8 (CHANGED from 3V3)** |
| MAX31856 | VIN 3V3, GND, SCK/SDO/SDI shared, CS GPIO0, T+/T- = pyro (swap if reading falls when hot) |
| EC11 encoder | A GPIO20, B GPIO21, C GND, switch GPIO3 + GND. Recommended: 1k series in the B line (GPIO21 is UART0 TX; the ROM prints boot text on it, and a closed B contact would short that output to GND briefly) |
| Light sensor | 10k from 3V3 to GPIO1; LDR from GPIO1 to GND; 100 nF GPIO1 to GND. If light/dark reads reversed, set `LDR_BRIGHT_IS_LOW = false`. Kit modules (KY-018 type): power from 3V3 only, never 5V. |
| Buzzer driver | GPIO2 -> R1 10k -> Q1 PNP (2N3906/S8550) base; R2 10k Q1 base -> 3V3; Q1 emitter 3V3; Q1 collector -> R3 2.2k -> Q2 NPN (2N2222/S8050/BC337) base; R4 10k Q2 base -> GND; Q2 emitter GND; Q2 collector -> buzzer (-); buzzer (+) -> 5V; No diode: the 3-24 V piezo was chosen tone buzzer (no coil), Nov 15, 2026. GPIO2 LOW = beep. R1+R2 pull GPIO2 up at reset = correct strap level and silent boot. |
| GPIO9 | Not used (BOOT button) |

Buzzer math: GPIO sinks ~0.3 mA; Q1 base ~0.19 mA for ~1.1 mA collector; Q2 base ~1 mA for 30 mA buzzer (forced beta ~30, saturated).

### Parts
In stock (inventory.csv): MLT-9650 / HN9650B 5 V active buzzers (row "Active SMD buzzers (10)": 3-7 V, 5 V rated, max 30 mA, >=85 dB at 10 cm, magnetic type so it needs D1); 2N3906/S8550 PNP and 2N2222/S8050/BC337 NPN (BOJACK + EEEEE TO-92 kits); 1N4148 likely in the 250-diode kit (check); resistors only as the ALLECIN 3 W kit (works, but bulky); photoresistor module likely in the ELEGOO 37-in-1 / SunFounder sensor kits (check).
Still to buy (no purchase made): Adafruit MAX31856 (3263) as before; nice to have: 1/4 W resistor kit (small parts for the pod), a bare GL5528-type LDR if the kit module is too big, 100 nF caps if none in stock. Optional: a louder 5 V piezo if 85 dB is too quiet over diesel noise (same driver).

### For CAD (pod)
Pod holds only the round LCD + EC11 (voltmeter dropped). v7 adds: a small window for the LDR facing the cabin, shielded from the LCD's own light; a sound hole for the 9.6 mm buzzer; the SuperMini's onboard blue LED (GPIO8) will glow with the backlight PWM, so cover it or keep it out of view. MLT-9650 is rated -20 to +70 C; a sun-loaded A-pillar may go past that.

### Open items
- Confirm on the bench: hold BOOT and plug in to reach download mode with BL wired to GPIO8 (BL may read low; normal boot is not affected). If it fails, unplug BL to recover, or flash over USB normally.
- Tune COOL AT on the truck at idle (educated guess 300 F).
- Check that 85 dB is loud enough in the cab.

## Future option: bigger screen (Nov 15, 2026)
v7 was kept with the Waveshare 1.28" for now. If it is too small in the truck, do a full redesign on the Waveshare ESP32-S3-LCD-2.8C no-touch (Amazon B0DX6TF5BN, $43.99, 480x480 ST7701 RGB, 95.86 mm round face). Free pins there are only GPIO0/19/20/43/44 + I2C (GPIO7/15), so: MCP9600 I2C amp replaces MAX31856, LDR on GPIO20 (ADC2, OK with BLE-only), encoder on GPIO43/44/0, onboard buzzer, GPIO19 spare. Touch version (B0DMJZPH2R) could replace the encoder.
