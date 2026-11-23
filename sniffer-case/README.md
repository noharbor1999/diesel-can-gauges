# sniffer-case

Two-piece printed case (base + lid) for the wired OBD-II CAN sniffer (see [`../obd2-sniff`](../obd2-sniff), and the wiring notes in [`../BUILD_GUIDE.md`](../BUILD_GUIDE.md)): Seeed XIAO ESP32-C6 + WCMCU-230 (SN65HVD230).

Outer size 50 × 35 × 20 mm (measured). Status 2026-11-22: v01 approved and exported (STEP/STL; 3MF can be re-exported from the CadQuery script).

- `src/can_case_v01.py`: CadQuery model, all parameters at the top (tagged OWNER = measured, SEEED = Seeed docs, EG = educated guess).
- `src/check_render_v01.py`: interference checks and preview renders → `exports/`.
- `src/export_v01.py`: 3MF (base + lid, print orientation), STEP and STL per part.

Print orientation, material, and starting slicer settings: [BUILD_GUIDE.md, Printing](../BUILD_GUIDE.md#printing). Default PLA for a bench fit; PETG or ASA is a better choice for a case that stays in the cab.
