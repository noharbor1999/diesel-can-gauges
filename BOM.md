# Bill of materials

Approximate street prices in USD, late 2026, rounded. Buy what you already have first. No order numbers. Quantities are for one USB sniffer plus one three-gauge cluster. A sniffer-only build can skip the gauge section.

Prices move. Treat them as budgeting figures, not quotes.

## A. USB CAN sniffer

| Item | Qty | Approx. | Generic source | Notes |
|---|---:|---:|---|---|
| Seeed XIAO ESP32-C6 | 1 | $8–15 | Seeed, Amazon | Main sniffer MCU. USB-C, 3.3 V I/O |
| WCMCU-230 (SN65HVD230) 3.3 V CAN transceiver | 1 | $5–8 | Amazon, AliExpress | Cut/remove the 120 Ω terminator |
| USB-C cable | 1 | $5 | Amazon | Data-capable |
| Hookup wire, 22–26 AWG, preferably twisted for CAN | as needed | $8 | Amazon | Silicone or PTFE is easier in a tight case |
| Momentary tact switch (optional marker) | 1 | $1 | Amazon | Between D5 and GND |
| 1 A inline fuse + holder (only if powering from OBD pin 16) | 1 | $5 | Amazon | Pin 16 is always-hot battery |
| 12 V to 5 V buck (only if not using USB power) | 1 | $3 | Amazon | e.g. MP1584EN fixed 5 V, 23 V max input |
| OBD-II pigtail or breakout (optional) | 1 | $10–25 | Amazon | Makes pins 6/14/5 easier to reach |
| M2 × 6 socket-cap screws | 4 | $4 (pack) | Amazon | Lid fasteners |
| M2 heat-set inserts | 4 | $6 (pack) | Amazon | Corner bosses in the base |
| Filament, PLA or PETG/ASA | ~20 g | $0.50 | Amazon, printer stock | See BUILD_GUIDE printing section |

Optional, multi-bus layout: more XIAO or ESP32-S3 nodes, one ESP32-S3 USB gateway, more transceivers. See `obd2-sniff/README.md`.

## B. Three-gauge cluster

| Item | Qty | Approx. | Generic source | Notes |
|---|---:|---:|---|---|
| Waveshare ESP32-S3-Touch-LCD-2.1 | 3 | $30–45 each | Waveshare, Amazon | 2.1 in round 480×480, ESP32-S3 on board |
| ESP32-S3-WROOM-1 DevKit, N16R8 (16 MB flash, 8 MB OPI PSRAM) | 1 | $10–15 | Amazon | Gauge hub |
| WCMCU-230 (SN65HVD230) | 1 | $5–8 | Amazon | Hub transceiver; terminator removed |
| MAX31855 K-type thermocouple amplifier, 3.3 V | 1 | $10–18 | Adafruit, Amazon | Firmware is written for MAX31855, not MAX31856 |
| K-type EGT / pyrometer probe | 1 | — | Existing, or diesel gauge suppliers | Cylinder 6, pre-turbo on this engine |
| EC11 rotary encoder with push switch | 1 | $2 | Amazon | Hub brightness / page / settings |
| MP1584EN fixed 5 V mini buck | 1 | $2–4 | Amazon | ~23 V max input. Size the 5 V rail for ~1 A |
| Add-a-circuit mini fuse tap + 1 A mini fuse | 1 | $8–12 | Amazon | Match the truck’s mini-fuse style; ignition-switched circuit |
| Ground ring terminal, 22–18 AWG, #10, heat-shrink | 1 | $5 (pack) | Amazon | Chassis ground |
| 26 AWG silicone / PTFE hookup wire | as needed | $8 | Amazon | |
| Heat shrink | as needed | $5 | Amazon | |
| 10 kΩ and 10 nF (optional, long encoder cable) | 2 each | $1 | Amazon, DigiKey | Pull-ups and debounce on encoder A/B |

The three displays can be powered from the same 5 V buck or from USB-C for bench work.

## C. Older 1.28 in single EGT prototype (optional / superseded)

Kept in `gauges/` for reference. Not required for the 2.1 in cluster.

| Item | Qty | Approx. | Generic source | Notes |
|---|---:|---:|---|---|
| ESP32-C3 SuperMini | 1 | $5 | Amazon | |
| Waveshare 1.28 in round GC9A01 LCD | 1 | $12–18 | Waveshare, Amazon | |
| MAX31856 K-type amplifier | 1 | $15–25 | Adafruit, Amazon | Different driver than MAX31855 |
| EC11 encoder, MP1584EN, fuse tap, ring terminal, wire | — | — | — | Same idea as the hub |
| Piezo tone buzzer 3–24 V + 2N3906 / 2N2222 driver | 1 | $3 | Amazon | Only this older firmware |
| LDR / photoresistor + 10 kΩ + 100 nF | 1 | $2 | Amazon | Auto-dim on the 1.28 in firmware |

## D. Tools (not consumed)

Arduino IDE 2, ESP32 board package 3.3.12, SavvyCAN, Python 3 with pandas / numpy / matplotlib / pyserial, a multimeter, and (for the case) a printer that can seat M2 inserts. A K-type thermometer (for example a PerfectPrime TC41) is useful to cross-check the EGT amp on the bench.
