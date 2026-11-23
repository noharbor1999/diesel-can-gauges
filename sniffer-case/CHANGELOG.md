# can-sniffer-case CHANGELOG

- v01 (2026-11-22 ET): first version. Preview approved as rendered; exported 2026-11-22 ~18:55 ET.
  - Outer 50 x 35 x 20 (base 17 + lid 3), walls/floor 2.0, inner cavity 46 x 31 x 15.
  - XIAO cradle: 2.0 plinth plus 4 L-shaped corner stops (21.5 x 18.3 pocket). USB-C end wall has a 12 x 7 stadium opening for the cable overmold (EG).
  - WCMCU-230 sits loose. Wire end has a 6.2 x 2.4 slot for 3 x 1.8 OD wires (EG).
  - Lid: 4 x M2 x 6 socket caps into M2 heat-set inserts in corner bosses.
  - Exports: geometry-only 3MF with base + lid as two objects in print orientation (base floor-down, lid outer face down) on the 256 plate; STEP per part (assembled frame); STL per part (print orientation).
  - Checks: both parts valid single solids and watertight; assembled 50.0 x 35.0 x 20.0. Base 10.4 cm3, lid 5.1 cm3.
  - Educated guesses: XIAO 21.0 x 17.8 (Seeed docs, not measured); 1.6 PCB; 3.2 top parts; USB-C 9 x 3.3 with 1.5 overhang; cable overmold 11.5 x 6.5 (12 x 7 opening); WCMCU-230 17 x 15 with a 10 mm resistor allowance; wires 1.8 OD; M2 insert hole 3.2 x 4.0.
  - Open items: XIAO has no hold-down (cable + a dab of hot glue); no strain relief on the 3 wires (knot inside).
