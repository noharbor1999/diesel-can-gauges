#!/usr/bin/env python3
"""2017 Ram 3500 CAN-C marker log (pins 6/14) — exhaust-brake switch and effort."""
from __future__ import annotations

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np
import pandas as pd

HERE = Path(__file__).resolve().parent
LOGS = HERE.parent / "data" / "logs"
PLOTS = HERE / "plots"
PLOTS.mkdir(exist_ok=True)
BLUE, ORANGE, GOLD, SKY = "#0072B2", "#E69F00", "#CC9A06", "#56B4E9"
MARKER_ID = 0x1FFFFFF0

# Button cycles OFF -> FULL ON -> AUTO -> OFF.
# Marks 15-19 at idle: OFF, FULL, AUTO, OFF, FULL.
SW = {0x00: "OFF", 0x80: "FULL", 0xA0: "AUTO"}


def arr(s):
    return np.asarray(s, dtype=np.int64)


def be16(h, l):
    return (arr(h) << 8) | arr(l)


def load(path: Path) -> pd.DataFrame:
    df = pd.read_csv(path, index_col=False)
    df = df.loc[:, ~df.columns.astype(str).str.startswith("Unnamed")]
    df["can_id"] = [int(str(x), 16) for x in df["ID"]]
    for col in ["D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8"]:
        if col not in df.columns:
            df[col] = np.nan
    for i, col in enumerate(["D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8"]):
        df[f"b{i}"] = [
            int(str(x), 16) if pd.notna(x) and str(x).strip() not in ("", "nan") else np.nan
            for x in df[col]
        ]
    ext = (
        df["Extended"].astype(str).str.lower().isin(("true", "1", "yes"))
        if "Extended" in df.columns
        else False
    )
    df["extended"] = ext
    df = df.sort_values("Time Stamp", kind="mergesort").reset_index(drop=True)
    ts = df["Time Stamp"].astype("int64")
    df["t"] = (ts - int(ts.min())) / 1e6
    return df


def of(df, cid):
    return df[df.can_id == cid].sort_values("t")


def main() -> None:
    path = LOGS / "pins_6-14_markers.csv"
    df = load(path)
    bus = df[df.can_id != MARKER_ID]
    marks = df[(df.can_id == MARKER_ID) & (df["extended"])].sort_values("t")
    marks = marks.copy()
    marks["marker"] = [int(x) for x in marks["b1"]]
    # 28-36 are a deliberate end burst — ignore for event decode
    used = marks[marks.marker <= 27]
    burst = marks[marks.marker >= 28]
    mark_t = {int(r.marker): float(r.t) for r in used.itertuples()}
    dur = float(df["t"].max())

    print(
        f"file {path.name}  frames={len(df)}  bus_frames={len(bus)}  "
        f"ids={bus.can_id.nunique()}  duration={dur:.3f}s  fps={len(df)/dur:.1f}"
    )
    print(
        f"has 0x108={0x108 in set(bus.can_id)}  has 0x322={0x322 in set(bus.can_id)}  "
        f"markers={len(marks)} (using 15-27, ignoring burst {len(burst)})"
    )

    s108 = of(bus, 0x108)
    t_rpm = s108["t"].to_numpy()
    rpm = be16(s108.b0, s108.b1).astype(float)

    s11c = of(bus, 0x11C)
    t_spd = s11c["t"].to_numpy()
    kph = be16(s11c.b4, s11c.b5).astype(float) * 0.0078125
    mph = kph / 1.609344
    ped = ((arr(s11c.b2) & 0x0F) << 8) | arr(s11c.b3)

    s120 = of(bus, 0x120)
    t_aps = s120["t"].to_numpy()
    aps = arr(s120.b2).astype(float) * 0.4

    s140 = of(bus, 0x140)
    t_brk = s140["t"].to_numpy()
    brk = np.isin(arr(s140.b0), (0x84, 0x85)).astype(int)  # 0x80 released, 0x85 pressed (known)

    s30a = of(bus, 0x30A)
    t30 = s30a["t"].to_numpy()
    b2 = arr(s30a.b2)
    b6 = arr(s30a.b6).astype(float)
    b7 = arr(s30a.b7).astype(float)
    sw_lvl = np.where(b2 == 0xA0, 2, np.where(b2 == 0x80, 1, np.where(b2 == 0x00, 0, np.nan)))

    s170 = of(bus, 0x170)
    t170 = s170["t"].to_numpy()
    tow = ((arr(s170.b1) & 0x02) != 0).astype(int)

    print(f"RPM {rpm.min():.0f}-{rpm.max():.0f}  speed {kph.min():.1f}-{kph.max():.1f} km/h ({mph.min():.1f}-{mph.max():.1f} mph)  APS {aps.min():.1f}-{aps.max():.1f}%")
    print("0x30A b2 changes (switch):")
    last = None
    for r in s30a.itertuples():
        v = int(r.b2)
        if v != last:
            print(f"  {r.t:8.3f}  0x{v:02X} {SW.get(v, '?')}")
            last = v

    events = []

    def ev(t, name, detail, conf):
        events.append({"t_s": round(t, 3), "event": name, "detail": detail, "confidence": conf})

    ev(6.852, "exhaust-brake switch FULL", "0x30A byte 2 0x00→0x80, 0.43 s before marker 16", "confirmed by markers")
    ev(12.153, "exhaust-brake switch AUTO", "0x30A byte 2 0x80→0xA0, 0.07 s before marker 17", "confirmed by markers")
    ev(17.053, "exhaust-brake switch OFF", "0x30A byte 2 0xA0→0x00, 0.47 s before marker 18", "confirmed by markers")
    ev(21.353, "exhaust-brake switch FULL", "0x30A byte 2 0x00→0x80, 0.35 s before marker 19. Stays 0x80 through the end of the log (no AUTO on the drive).", "confirmed by markers")
    ev(3.263, "marker 15 OFF", "Already 0x00 at log start; no byte change. Idle, 0 mph.", "confirmed by markers")
    ev(7.283, "marker 16 FULL", "Idle, 0 mph. Switch changed at 6.852 s.", "confirmed by markers")
    ev(12.223, "marker 17 AUTO", "Idle, 0 mph. Switch changed at 12.153 s.", "confirmed by markers")
    ev(17.518, "marker 18 OFF", "Idle, 0 mph. Switch changed at 17.053 s.", "confirmed by markers")
    ev(21.698, "marker 19 FULL", "Idle, 0 mph. Switch changed at 21.353 s.", "confirmed by markers")
    ev(29.050, "Tow/Haul candidate ON", "0x170 byte 1 252→254 (bit 1). After the park switch clicks, as he starts to roll (first mph>1 at 27.03 s). Same procedural moment as IHS 0x30A b4 FC→FE.", "educated guess")
    ev(73.681, "first marked-drive pull starts", "APS rises; unmarked driveway crawl / service-brake stops already happened 24–73 s.", "confirmed (0x120 / 0x11C / 0x140)")
    ev(79.793, "marker 20 — mid first pull", "25.8 mph, APS 58%, 0x30A b7=0, still FULL. Not a let-off and not an AUTO switch. Extra click during the accel up to event 1.", "confirmed by markers + APS/speed")
    ev(87.593, "marker 21 — let-off 1 (FULL)", "APS already 0 (dropped ~86.52 s). 0x30A b7 rises at 88.16 s to 54–58. Coast 51.2→41.5 mph. No service brake.", "confirmed by markers")
    ev(94.828, "marker 22 — back on throttle", "APS 46%, b7=0, speed rises.", "confirmed by markers")
    ev(99.773, "marker 23 — let-off 2 (FULL)", "APS=0, b7 rises to 51–56. Coast 50.5→40.4 mph.", "confirmed by markers")
    ev(105.238, "marker 24 — back on throttle", "APS 45%, b7=0.", "confirmed by markers")
    ev(110.118, "marker 25 — let-off 3 (FULL)", "APS=0, b7 rises to 49–55. Coast 49.3→39.5 mph.", "confirmed by markers")
    ev(116.163, "marker 26 — back on throttle", "APS 40%, b7=0.", "confirmed by markers")
    ev(120.893, "marker 27 — let-off 4 (FULL)", "APS=0, b7 rises to 47–52. No closing throttle mark. Service brake at 126.82 s, then markers 28–36 end burst (ignored).", "confirmed by markers")
    ev(126.819, "service brake to end", "0x140 0x85 and 0x11C pedal from 126.80 s. Markers 28–36 at 127.24–128.85 s are a deliberate burst, ignored.", "confirmed")

    evdf = pd.DataFrame(events).sort_values("t_s")
    ev_path = HERE / "events_pins_6-14_markers.csv"
    evdf.to_csv(ev_path, index=False)
    print(f"wrote {ev_path}")

    # ---- plot ----
    fig, ax = plt.subplots(4, 1, figsize=(12.5, 11), sharex=True, gridspec_kw={"height_ratios": [1.15, 1, 1.1, 0.7]})
    fig.suptitle(
        "CAN-C marker log (pins 6/14) — 2017 Ram 3500\n"
        "0x30A byte 2 switch confirmed: 0x00 OFF, 0x80 FULL, 0xA0 AUTO. "
        "Drive stayed FULL. Gold lines = markers 15–27 (28–36 burst ignored).",
        fontsize=11,
    )

    let_off = [(87.593, 94.828), (99.773, 105.238), (110.118, 116.163), (120.893, 126.819)]
    brake_spans = [(24.156, 42.106), (46.804, 48.443), (58.558, 61.277), (64.695, 72.811), (126.803, dur)]
    for a in ax:
        for s0, s1 in brake_spans:
            a.axvspan(s0, s1, color="#56B4E9", alpha=0.18, zorder=0)
        for s0, s1 in let_off:
            a.axvspan(s0, s1, color="#E69F00", alpha=0.16, zorder=0)
    if len(burst):
        ax[0].axvspan(float(burst.t.min()), float(burst.t.max()), color="#444444", alpha=0.15, zorder=0)

    ax[0].plot(t_spd, kph, color=SKY, lw=1.2, label="0x11C speed km/h (known, opendbc)")
    ax[0].plot(t_spd, mph, color=GOLD, lw=0.9, ls="--", label="same, mph")
    ax[0].plot(t_rpm, rpm / 40.0, color=BLUE, lw=0.9, label="0x108 RPM / 40  (known; ÷40 for overlay only)")
    ax[0].set_ylabel("speed / RPM÷40")

    ax[1].plot(t_aps, aps, color=ORANGE, lw=1.2, label="0x120 APS %  (known, byte2 × 0.4)")
    ax[1].step(t_brk, brk * 50, where="post", color=BLUE, lw=1.4, label="0x140 service brake ×50  (0x85 pressed, known)")
    ax[1].set_ylabel("APS % / brake")

    ax[2].step(t30, sw_lvl, where="post", color=BLUE, lw=1.8, label="0x30A byte 2  exhaust-brake SWITCH (confirmed by markers 15–19)")
    ax[2].set_yticks([0, 1, 2])
    ax[2].set_yticklabels(["OFF 0x00", "FULL 0x80", "AUTO 0xA0"])
    ax[2].plot(t30, b7 / 30.0 + 0, color=ORANGE, lw=1.2, label="0x30A byte 7  effort / active  (raw / 30, +offset 0)  confirmed-by-markers in FULL coasts")
    # plot b7 on a twin? mixing 0-2 switch with b7/30 (0-2.1) works — b7/30 max 63/30=2.1 sits on AUTO row. Better use twin y.
    ax[2].clear()
    for s0, s1 in brake_spans:
        ax[2].axvspan(s0, s1, color="#56B4E9", alpha=0.18, zorder=0)
    for s0, s1 in let_off:
        ax[2].axvspan(s0, s1, color="#E69F00", alpha=0.16, zorder=0)
    ax[2].step(t30, sw_lvl, where="post", color=BLUE, lw=2.0, label="0x30A byte 2  SWITCH  (confirmed: 0x00 OFF, 0x80 FULL, 0xA0 AUTO)")
    ax[2].set_ylabel("switch")
    ax[2].set_yticks([0, 1, 2])
    ax[2].set_yticklabels(["OFF 0x00", "FULL 0x80", "AUTO 0xA0"])
    ax[2].set_ylim(-0.3, 2.5)
    ax2b = ax[2].twinx()
    ax2b.plot(t30, b7, color=ORANGE, lw=1.3, label="0x30A byte 7  effort (raw counts; 0 idle/throttle, 47–58 on FULL coasts)")
    ax2b.set_ylabel("0x30A b7", color=ORANGE)
    ax2b.set_ylim(-2, 70)
    # combined legend
    h2, l2 = ax[2].get_legend_handles_labels()
    h2b, l2b = ax2b.get_legend_handles_labels()
    ax[2].legend(h2 + h2b, l2 + l2b, loc="upper left", fontsize=8, frameon=True)

    ax[3].step(t170, tow, where="post", color=ORANGE, lw=1.8, label="0x170 byte 1 bit 1  Tow/Haul candidate (0=252, 1=254)  educated guess")
    ax[3].axvline(29.050, color=GOLD, lw=1.2, ls=":", label="Tow/Haul candidate on at 29.05 s")
    ax[3].plot(t30, b6 / 80.0, color=SKY, lw=0.9, ls="--", label="0x30A byte 6 boost candidate / 80  (educated guess, overlay)")
    ax[3].set_ylabel("Tow/Haul")
    ax[3].set_yticks([0, 1])
    ax[3].set_yticklabels(["off (252)", "on (254)"])
    ax[3].set_xlabel("time (s from log start)")

    for n, t in mark_t.items():
        for a in ax:
            a.axvline(t, color=GOLD, lw=0.8, alpha=0.85, zorder=3)
    ax[0].set_ylim(top=92)
    for n, t in mark_t.items():
        ax[0].text(t, 90.5, str(n), color=GOLD, fontsize=7, ha="center", va="top", fontweight="bold")

    handles = [
        mpatches.Patch(color="#E69F00", alpha=0.35, label="FULL let-off windows (M21–22, 23–24, 25–26, 27–brake)"),
        mpatches.Patch(color="#56B4E9", alpha=0.35, label="service-brake windows (0x11C / 0x140)"),
        mpatches.Patch(color="#444444", alpha=0.25, label="markers 28–36 end burst (ignored)"),
    ]
    h0, l0 = ax[0].get_legend_handles_labels()
    ax[0].legend(h0 + handles, l0 + [h.get_label() for h in handles], loc="upper left", fontsize=7.5, frameon=True)
    ax[1].legend(loc="upper left", fontsize=8, frameon=True)
    ax[3].legend(loc="upper left", fontsize=8, frameon=True)

    for a in ax:
        a.grid(True, alpha=0.25)
        a.set_xlim(0, dur)

    fig.tight_layout()
    out = PLOTS / "canc_markers.png"
    fig.savefig(out, dpi=120)
    plt.close()
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
