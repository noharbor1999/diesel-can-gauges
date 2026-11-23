#!/usr/bin/env python3
"""List capture-marker frames from a SavvyCAN CSV.

The XIAO ESP32-C6 sniffer (firmware v3) injects one extended frame per button
press (pad D5 / GPIO23, or BOOT / GPIO9):

    ID        1FFFFFF0   (extended; cannot collide with 11-bit Ram traffic)
    DLC       8
    D1        AA
    D2        marker number (01, 02, 03... resets when the board boots)
    D3-D6     millis() little-endian (node uptime, not the SavvyCAN timestamp)
    D7 D8     55 55

Times below are seconds from the earliest timestamp in the file (the log is
sorted first, in case SavvyCAN rows are out of order).

Usage:
    python3 /workspace/maker/projects/obd2-sniff/decode/markers.py path/to/log.csv
"""
from __future__ import annotations
import argparse
import sys
from pathlib import Path

import pandas as pd


MARKER_ID = 0x1FFFFFF0


def _hex_id(val) -> int:
    s = str(val).strip()
    if s.lower().startswith("0x"):
        s = s[2:]
    return int(s, 16)


def _hex_byte(val) -> int:
    s = str(val).strip()
    if s.lower().startswith("0x"):
        s = s[2:]
    return int(s, 16)


def list_markers(path: Path) -> list[dict]:
    df = pd.read_csv(path, index_col=False)
    df = df.loc[:, ~df.columns.astype(str).str.startswith("Unnamed")]
    if "Time Stamp" not in df.columns or "ID" not in df.columns:
        raise SystemExit(f"{path}: not a SavvyCAN CSV (need Time Stamp and ID columns)")
    df["can_id"] = [_hex_id(x) for x in df["ID"]]
    ext_col = df["Extended"] if "Extended" in df.columns else None
    if ext_col is not None:
        ext = ext_col.astype(str).str.lower().isin(("true", "1", "yes"))
        hits = df[(df["can_id"] == MARKER_ID) & ext]
    else:
        hits = df[df["can_id"] == MARKER_ID]
    hits = hits.sort_values("Time Stamp")
    if hits.empty:
        return []
    t0 = int(df["Time Stamp"].astype("int64").min())
    rows = []
    for _, r in hits.iterrows():
        t_s = (int(r["Time Stamp"]) - t0) / 1e6
        d1 = _hex_byte(r["D1"]) if "D1" in r and pd.notna(r["D1"]) else None
        num = _hex_byte(r["D2"]) if "D2" in r and pd.notna(r["D2"]) else None
        ms = None
        if all(c in r.index and pd.notna(r[c]) for c in ("D3", "D4", "D5", "D6")):
            ms = (
                _hex_byte(r["D3"])
                | (_hex_byte(r["D4"]) << 8)
                | (_hex_byte(r["D5"]) << 16)
                | (_hex_byte(r["D6"]) << 24)
            )
        rows.append(
            {
                "marker": num,
                "t_s": t_s,
                "t_us": int(r["Time Stamp"]) - t0,
                "aa": d1,
                "millis": ms,
            }
        )
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description="List SavvyCAN capture markers (ID 1FFFFFF0 ext).")
    ap.add_argument("csv", type=Path, help="SavvyCAN CSV log")
    args = ap.parse_args()
    if not args.csv.is_file():
        print(f"not found: {args.csv}", file=sys.stderr)
        return 1
    rows = list_markers(args.csv)
    if not rows:
        print(f"no marker frames (ID 1FFFFFF0, Extended=true) in {args.csv}")
        return 0
    print(f"# {len(rows)} marker(s) in {args.csv}")
    print("marker  t_s")
    for r in rows:
        extra = ""
        if r["aa"] is not None and r["aa"] != 0xAA:
            extra += f"  (D1={r['aa']:02X}, expected AA)"
        if r["millis"] is not None:
            extra += f"  node_millis={r['millis']}"
        print(f"{r['marker']:>6}  {r['t_s']:10.3f}{extra}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
