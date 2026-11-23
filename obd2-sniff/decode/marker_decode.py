#!/usr/bin/env python3
"""2017 Ram 3500 CAN-IHS marker log (pins 3/11) — exhaust-brake switch hunt."""
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

# the owner: the dash button cycles OFF -> FULL ON -> AUTO -> OFF.
# Marks 1-3 at idle are interpreted in that order (not off/auto/full).
PARK_STATES = {1: "OFF", 2: "FULL ON", 3: "AUTO"}


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
    ext = df["Extended"].astype(str).str.lower().isin(("true", "1", "yes")) if "Extended" in df.columns else False
    df["extended"] = ext
    df = df.sort_values("Time Stamp", kind="mergesort").reset_index(drop=True)
    ts = df["Time Stamp"].astype("int64")
    df["t"] = (ts - int(ts.min())) / 1e6
    return df


def of(df, cid):
    return df[df.can_id == cid].sort_values("t")


def marker_rows(df: pd.DataFrame) -> pd.DataFrame:
    m = df[(df.can_id == MARKER_ID) & (df["extended"])].copy()
    m = m.sort_values("t")
    m["marker"] = [int(x) if pd.notna(x) else np.nan for x in m["b1"]]
    return m


def nearest(t, y, at):
    i = int(np.argmin(np.abs(t - at)))
    return float(t[i]), (float(y[i]) if np.isfinite(y[i]) else np.nan)


def main() -> None:
    path = LOGS / "pins_3-11_markers.csv"
    df = load(path)
    bus = df[df.can_id != MARKER_ID]
    marks = marker_rows(df)
    mark_t = {int(r.marker): float(r.t) for r in marks.itertuples()}

    dur = float(df["t"].max())
    n_ids = int(bus.can_id.nunique())
    print(f"file {path.name}  frames={len(df)}  bus_frames={len(bus)}  ids={n_ids}  duration={dur:.3f}s  fps={len(df)/dur:.1f}")
    print(f"has 0x322={0x322 in set(bus.can_id)}  has 0x108={0x108 in set(bus.can_id)}  markers={len(marks)}")
    for r in marks.itertuples():
        print(f"  marker {int(r.marker):2d}  t={r.t:8.3f}s  node_ms={int(r.b2)|int(r.b3)<<8|int(r.b4)<<16|int(r.b5)<<24}")

    s = of(bus, 0x322)
    t322 = s["t"].to_numpy()
    rpm = be16(s.b0, s.b1).astype(float)
    rpm = np.where((rpm == 0xFFFF) | (rpm == 0), np.nan, rpm)
    mph = be16(s.b2, s.b3).astype(float) / 200.0  # known magikh0e/JL
    kph = mph * 1.609344
    gear = arr(s.b4)

    aps = of(bus, 0x24E)
    t_aps, aps_b0 = aps["t"].to_numpy(), arr(aps.b0).astype(float)
    aps_b0 = np.where(aps_b0 >= 200, np.nan, aps_b0)  # 0xFF at key-off, not throttle

    brk = of(bus, 0x332)
    t_brk, brk_b2 = brk["t"].to_numpy(), arr(brk.b2)
    brk_flag = ((brk_b2 & 0x40) != 0).astype(int)  # confirmed: 0x18/0x1C off, 0x58/0x5C on

    tha = of(bus, 0x30A)
    t_tha, tha_b4 = tha["t"].to_numpy(), arr(tha.b4)
    # 0xFF is a key-off/init filler (bit 1 set); only 0xFC vs 0xFE is the mode flag.
    tow = np.where(tha_b4 == 0xFE, 1, np.where(tha_b4 == 0xFC, 0, np.nan)).astype(float)

    loadsig = of(bus, 0x2C3)
    t_ld, b5 = loadsig["t"].to_numpy(), arr(loadsig.b5)
    load_s = np.where(b5 >= 128, b5 - 256, b5).astype(float)

    print(f"0x322 RPM {np.nanmin(rpm):.0f}-{np.nanmax(rpm):.0f}  speed {mph.min():.2f}-{mph.max():.2f} mph ({kph.min():.1f}-{kph.max():.1f} km/h)")
    moving = mph > 1.0
    print(f"first rpm>200 at {t322[np.where(rpm > 200)[0][0]]:.3f}s  first mph>1 at {t322[np.where(moving)[0][0]]:.3f}s")

    # Per-marker kinematics
    print("\nmarker kinematics (0x322 + 0x24E b0 + 0x332 brake):")
    times = [0.0] + [mark_t[i] for i in sorted(mark_t)] + [dur]
    labels = ["start"] + [f"M{i}" for i in sorted(mark_t)] + ["end"]
    for i in range(len(times) - 1):
        a, b = times[i], times[i + 1]
        m = (t322 >= a) & (t322 < b)
        if not m.any():
            continue
        # APS mode in window
        ma = (t_aps >= a) & (t_aps < b)
        aps_mean = float(aps_b0[ma].mean()) if ma.any() else float("nan")
        mb = (t_brk >= a) & (t_brk < b)
        brk_on = int(brk_flag[mb].max()) if mb.any() else 0
        print(
            f"  {labels[i]:6s} {a:7.3f} -> {labels[i+1]:4s} {b:7.3f}  "
            f"mph {mph[m][0]:5.1f}->{mph[m][-1]:5.1f} (min {mph[m].min():5.1f} max {mph[m].max():5.1f})  "
            f"rpm {np.nanmin(rpm[m]):.0f}-{np.nanmax(rpm[m]):.0f}  "
            f"APS_b0~{aps_mean:.1f}  svc_brake={brk_on}"
        )

    # events csv
    events = []
    def ev(t, name, detail, conf):
        events.append({"t_s": round(t, 3), "event": name, "detail": detail, "confidence": conf})

    ev(12.38, "key RUN / crank", "0x122 04 then 05/15; first 0x322 RPM>200 at 13.66 s", "known ID; byte values educated guess")
    for n, t in mark_t.items():
        _, r = nearest(t322, rpm, t)
        _, sp = nearest(t322, mph, t)
        _, a0 = nearest(t_aps, aps_b0.astype(float), t)
        if n in PARK_STATES:
            ev(t, f"marker {n}", f"idle 0 mph, ~{r:.0f} rpm. Intended exhaust-brake switch = {PARK_STATES[n]} (button cycle OFF→FULL→AUTO→OFF). No IHS 3-state byte found.", "marker time confirmed; switch state from the button cycle, not a found byte")
        else:
            ev(t, f"marker {n}", f"{sp:.1f} mph, ~{r:.0f} rpm, 0x24E b0≈{a0:.0f}", "confirmed by markers + 0x322 / 0x24E")
    ev(48.261, "Tow/Haul candidate ON", "IHS 0x30A byte 4 0xFC→0xFE (bit 1), stays 0xFE. Same pattern as CAN-C 0x170 byte 1 on the earlier drive. Was 0xFC the whole running portion of run 2.", "educated guess (strengthened by run-2 contrast)")
    ev(50.331, "service brake + shift out of Park", "0x332 byte 2 0x18→0x58 (bit 6). 0x322 byte 4 0x34→0x35→0x01→0x2D (Park/Neutral/Reverse educated guess). Creep starts 57.86 s.", "brake flag confirmed; gear educated guess")
    ev(72.26, "unmarked first pull then service-brake stop", "Accel ~72-80 s to ~25 mph; 0x332 bit 6 on 85.74-92.19 s, stopped by ~90 s.", "confirmed by 0x322 / 0x332")
    ev(92.19, "second pull (unmarked coast 102-110 s)", "Hard accel 92-102 s, peak RPM 2626 at 99.36 s, 43.9 mph; mild unmarked coast 102-110 s (no service brake).", "confirmed by 0x322 / 0x332")
    ev(117.018, "braking event 1 starts (M4 let-off)", "45.2→38.7 mph in 3.27 s, 0x24E b0=0, no service brake.", "confirmed by markers")
    ev(120.288, "back on throttle (M5)", "Speed rises; 0x24E b0 up.", "confirmed by markers")
    ev(125.923, "braking event 2 starts (M6 let-off)", "Short 1.83 s, 42.4→39.5 mph, 0x24E b0=0, no service brake. Real let-off, not a dead click.", "confirmed by markers")
    ev(127.748, "back on throttle (M7)", "Accel to log max 55.03 mph at 133.26 s.", "confirmed by markers")
    ev(134.373, "braking event 3 starts (M8 let-off)", "53.7→44.4 mph in 5.15 s, 0x24E b0≈0, no service brake.", "confirmed by markers")
    ev(139.523, "back on throttle (M9)", "Speed rises again.", "confirmed by markers")
    ev(143.988, "M10 — intended 'switched to AUTO'", "Kinematically a let-off (49.8→45 mph, APS drops). No IHS switch-byte change found. If M3 already left the switch in AUTO, this is more likely the first auto-event let-off (or a switch click we cannot see on IHS).", "kinematics confirmed; switch change not found on IHS")
    ev(147.533, "M11 — brief throttle", "0x24E b0 rises, speed nearly flat (~45-47 mph).", "confirmed by markers")
    ev(149.873, "M12 then service-brake stop", "0x332 bit 6 on at 150.54 s; 45→6 mph by end. Marks 13-16 never happened — log was cut short.", "confirmed by markers + 0x332")

    evdf = pd.DataFrame(events)
    ev_path = HERE / "events_pins_3-11_markers.csv"
    evdf.to_csv(ev_path, index=False)
    print(f"wrote {ev_path}")

    # ---- plot ----
    fig, ax = plt.subplots(4, 1, figsize=(12.5, 11), sharex=True, gridspec_kw={"height_ratios": [1.15, 1, 1, 0.7]})
    fig.suptitle(
        "IHS marker log (pins 3/11) — 2017 Ram 3500\n"
        "Markers 1–3 at idle: OFF, FULL ON, AUTO  (button cycles OFF→FULL→AUTO→OFF). "
        "Vertical gold lines = marker presses.",
        fontsize=11,
    )

    # service-brake and marked-coast spans
    brake_spans = [(50.331, 70.883), (85.742, 92.192), (150.541, dur)]
    coast_spans = [(117.018, 120.288), (125.923, 127.748), (134.373, 139.523)]
    for a in ax:
        for s0, s1 in brake_spans:
            a.axvspan(s0, s1, color="#56B4E9", alpha=0.18, zorder=0)
        for s0, s1 in coast_spans:
            a.axvspan(s0, s1, color="#E69F00", alpha=0.16, zorder=0)

    ax[0].plot(t322, kph, color=SKY, lw=1.2, label="0x322 speed km/h (known, magikh0e/JL, /200 mph)")
    ax[0].plot(t322, mph, color=GOLD, lw=0.9, ls="--", label="same, mph")
    ax[0].plot(t322, rpm / 40.0, color=BLUE, lw=0.9, label="0x322 RPM / 40  (known; ÷40 for overlay only)")
    ax[0].set_ylabel("speed / RPM÷40")
    ax[0].legend(loc="upper left", fontsize=8, frameon=True)

    ax[1].plot(t_aps, aps_b0, color=ORANGE, lw=1.2, label="0x24E byte 0  throttle/APS-like (educated guess; 0 on marked let-offs)")
    ax[1].step(t_brk, brk_flag * 20, where="post", color=BLUE, lw=1.4, label="0x332 byte 2 bit 6  service brake ×20 (confirmed)")
    ax[1].set_ylabel("APS-like / brake")
    ax[1].legend(loc="upper left", fontsize=8, frameon=True)

    ax[2].step(t322, gear, where="post", color=BLUE, lw=1.2, label="0x322 byte 4  gear candidate (educated guess: 0x34 Park, 0x2D Rev, 4/8/12/16 = 1st–4th)")
    ax[2].plot(t_ld, load_s, color=ORANGE, lw=0.9, label="0x2C3 byte 5 as int8  signed load / long. accel (educated guess; not EB-specific)")
    ax[2].axhline(0, color="#444444", lw=0.4)
    ax[2].set_ylabel("gear / signed b5")
    ax[2].legend(loc="upper left", fontsize=8, frameon=True)

    ax[3].step(t_tha, tow, where="post", color=ORANGE, lw=1.8, label="0x30A byte 4 bit 1  Tow/Haul candidate (0=0xFC, 1=0xFE)  educated guess")
    ax[3].axvline(48.261, color=GOLD, lw=1.2, ls=":", label="Tow/Haul candidate on at 48.26 s")
    ax[3].set_ylabel("Tow/Haul")
    ax[3].set_yticks([0, 1])
    ax[3].set_yticklabels(["off (FC)", "on (FE)"])
    ax[3].set_xlabel("time (s from log start)")
    ax[3].legend(loc="upper left", fontsize=8, frameon=True)

    # marker lines + numbers on every panel
    ymin0, ymax0 = ax[0].get_ylim()
    for n, t in mark_t.items():
        for a in ax:
            a.axvline(t, color=GOLD, lw=0.8, ls="-", alpha=0.85, zorder=3)
        ax[0].text(t, ymax0 - 2, str(n), color=GOLD, fontsize=8, ha="center", va="top", fontweight="bold")

    handles = [
        mpatches.Patch(color="#E69F00", alpha=0.35, label="marked let-off windows (M4–M5, M6–M7, M8–M9)"),
        mpatches.Patch(color="#56B4E9", alpha=0.35, label="service-brake windows (0x332 bit 6)"),
    ]
    ax[0].legend(loc="upper left", fontsize=8, frameon=True)
    ax[3].legend(handles=ax[3].get_legend_handles_labels()[0] + handles if False else ax[3].get_legend_handles_labels()[0], loc="upper left", fontsize=8)
    # put span legend on panel 0 by re-adding
    h0, l0 = ax[0].get_legend_handles_labels()
    ax[0].legend(h0 + handles, l0 + [h.get_label() for h in handles], loc="upper left", fontsize=8, frameon=True)

    for a in ax:
        a.grid(True, alpha=0.25)
        a.set_xlim(0, dur)

    fig.tight_layout()
    out = PLOTS / "ihs_markers.png"
    fig.savefig(out, dpi=120)
    plt.close()
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
