#!/usr/bin/env python3
"""Decode SavvyCAN listen-only logs from a 2017 Ram 3500 OBD2 port.

Reads:
  ../data/logs/pins_6-14.csv
  ../data/logs/pins_3-11.csv

Writes ID tables, curated event lists, decoded signal CSVs and PNG plots
into this folder. Re-run with: python3 decode_logs.py

Public sources used (IDs can differ by FCA platform and year; every
decode is labeled known or educated guess):
  - commaai opendbc chrysler_ram_hd_generated.dbc
  - magikh0e.pl FCA CAN Bus & Message Reference (mostly Jeep)
  - JL Wrangler CAN Bus Reverse community notes
  - CarGeek Chrysler/Dodge/Jeep powertrain ID map
  - Open Labor Project / MACS: DLC pins 6/14 = CAN-C 500 kbit/s,
    pins 3/11 = CAN-IHS 125 kbit/s on later FCA trucks
"""
from __future__ import annotations

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

HERE = Path(__file__).resolve().parent
LOGS = HERE.parent / "data" / "logs"
PLOTS = HERE / "plots"
PLOTS.mkdir(exist_ok=True)

# Okabe-Ito / colorblind-safe (no red-green as the only distinction)
BLUE = "#0072B2"
ORANGE = "#E69F00"
GOLD = "#CC9A06"
SKY = "#56B4E9"
BLACK = "#000000"

# ---------------------------------------------------------------------------
# Function guesses. confidence is "known (source)", "educated guess (basis)",
# or "" if we are not claiming a function.
# ---------------------------------------------------------------------------
CANC_GUESS = {
    0x102: ("STEERING: steering angle / rate / checksum",
            "known (opendbc chrysler_ram_hd BO_258; 10 ms, DLC 8 matches)"),
    0x108: ("ECM_1: ENGINE_RPM bytes 0-1 big-endian, scale 1 rpm/bit; torque in later bits; counter/checksum in b6/b7",
            "known (opendbc chrysler_ram_hd BO_264; verified: 0 until 63.75 s, crank to 942, idle ~700, blip to 2664)"),
    0x10C: ("ESP_2: ESC / ACC torque request (opendbc BO_268)",
            "known (opendbc chrysler_ram_hd ID and DLC match; not byte-verified on this log)"),
    0x118: ("ECM_TRQ: engine torque min/max (opendbc BO_280)",
            "known (opendbc chrysler_ram_hd ID and 10 ms / DLC 8 match; not byte-verified)"),
    0x11C: ("ESP_8: brake pressure / BRAKE_PEDAL (12-bit) and Vehicle_Speed bytes 4-5 * 0.0078125 km/h",
            "known (opendbc chrysler_ram_hd BO_284; speed stayed 0.0; pedal raw rose 0-1654 at 108.70-112.74 s)"),
    0x120: ("ECM_2: Accelerator_Position = byte 2 * 0.4 percent; b5 counter, b6 checksum-like",
            "known (opendbc chrysler_ram_hd BO_288 DLC 7; verified: 0 at idle, 24% then 66% with RPM blips)"),
    0x140: ("ESP_1: Brake_Pedal_State in byte 0 bits 2-3; Vehicle_Speed also in this frame",
            "known (opendbc chrysler_ram_hd BO_320; byte 0 0x80 idle / 0x85 pressed, same window as 0x11C)"),
    0x158: ("ESP_6: four wheel speeds (opendbc BO_344)",
            "known (opendbc chrysler_ram_hd ID / 20 ms / DLC 8 match; truck was stationary so speeds not exercised)"),
    0x170: ("Transmission_Status: gear state",
            "known (opendbc chrysler_ram_hd BO_368; not independently exercised, truck stayed in park)"),
    0x1C0: ("RKE / lock-unlock family (body message, often gateway-rebroadcast on CAN-C)",
            "known ID (magikh0e $1C0); this log stayed at idle 00 00 00 80 11 48, no lock/unlock command codes"),
    0x1D0: ("ORC_1: seatbelt / restraints",
            "known (opendbc chrysler_ram_hd BO_464; not exercised)"),
    0x23A: ("CRUISE_BUTTONS (3-byte): byte 0 switches, byte 1 counter, byte 2 checksum",
            "known (opendbc chrysler_ram_hd BO_570; byte 0 stayed 0x00, no cruise presses)"),
    0x318: ("STEERING_LEVERS: byte 0 turn signals / high-beam; byte 2 tracks wiper-like step values",
            "known ID (opendbc ram_hd TURN_SIGNALS / HIGH_BEAM_PRESSED; JL notes also put wipers here). "
            "Byte 0: 0x04 coincides with 0x334 high-beam display; 0x01 and 0x02 coincide with 0x334 turn-lamp blinks. "
            "Which of 0x01/0x02 is left vs right is unconfirmed (JL says 0x01=left, 0x02=right; 5th-gen BCM bit names disagree). "
            "Byte 2 wiper steps are an educated guess (values 0,1,2,3,4,5,0x0D,0x0E in one rising/falling burst)."),
    0x334: ("BCM_1: body status. Byte 6 is the dash-illumination / dimmer candidate (0x00 off, 0xC8=200 on). "
            "Byte 7 bit 2 tracks high beam display. Byte 3 bits 6 and 7 blink with the turn-signal lamps.",
            "known ID (opendbc ram_hd BCM_1 doors / turn lamps / high-beam display). "
            "Byte 6 dimmer: educated guess (value 0xC8 is the documented Jeep $291 dimmer maximum; "
            "only 0x00 and 0xC8 appear in this log, so proportional travel of the dimmer wheel is not yet proven)."),
    0x214: ("Present on both buses; bytes 0 and 2 wander slowly and dip at crank. Not a clean light switch.",
            "educated guess only as a related body/power signal; not the Jeep $291 lighting layout"),
    0x122: ("Same ID as IHS ignition, but DLC 6 and almost constant on CAN-C. Do not use the IHS $122 map here.",
            "unknown on this bus (ID collision / different payload)"),
    0x2C2: ("Same ID as IHS battery voltage, but DLC 8 and static on CAN-C. Do not use the IHS $2C2 map here.",
            "unknown on this bus"),
}

IHSS_GUESS = {
    0x122: ("Ignition / key state, byte 0 (and byte 1). On this truck: 0x00 early, 0x03 brief, "
            "0x04 = key RUN (from 6.10 s, engine still stopped), 0x05 then 0x15 around crank 36.12-37.00 s, "
            "then 0x04 with engine running.",
            "known ID (magikh0e $122, Jeep codes). Byte values on this truck are an educated guess "
            "from correlation with 0x322 RPM (Jeep lists 0x0402 as 'engine running'; here 0x04 is key RUN, engine may be off)."),
    0x1C0: ("RKE / lock-unlock family, 6-byte, ~200 ms. Idle payload 00 00 00 80 11 48. "
            "Byte 4 0x11->0x51 at 6.12-9.13 s tracks the key going to RUN, not a lock command.",
            "known ID (magikh0e $1C0). Jeep lock codes (byte 0 = 0x21/0x23/0x24) did not appear; locks were not clearly logged."),
    0x2C2: ("Battery voltage = byte 2 / 10 volts (DLC 4). 12.5-12.7 V key-on, sag to 10.6 V at crank, 13.7-14.5 V running.",
            "known (magikh0e $2C2; formula confirmed by crank sag and alternator rise on this log)"),
    0x2D3: ("Network-management wake / center-dash buttons. Payload stayed 07 00 00 00 00 00 00 00 (no button presses).",
            "known (magikh0e $2D3)"),
    0x2FA: ("Lighting / dimmer candidate. Default 01 01 00 18 C8 C8 C8 00. "
            "While lights were on (59.49-61.70 s): 01 00 C8 18 C8 C8 C8 00 (byte 2 = 0xC8, byte 1 = 0).",
            "educated guess (ID is a JL lighting/window candidate; byte 2 uses the documented Jeep dimmer maximum 0xC8 "
            "and moves with 0x3B2 lighting flags). Only 0x00/0xC8 seen — sweep the dimmer wheel to confirm it is a level."),
    0x318: ("Steering-column levers and wiper-like byte 2. Same layout as CAN-C 0x318.",
            "known ID (opendbc / JL). See the CAN-C 0x318 note for left/right and wiper caveats."),
    0x322: ("Cluster / body RPM and speed. RPM = bytes 0-1 big-endian (0xFFFF or 0 = engine off). "
            "Speed = bytes 2-3 / 200 mph (stayed 0; truck was parked).",
            "known (magikh0e / JL Wrangler notes; RPM matches 0x108 on the other capture: off, then ~700 idle, blip >2000)"),
    0x332: ("Brake / gear info candidate. Byte 2 0x18 usual, 0x58 in pulses at 78.06-82.75 s.",
            "educated guess (JL V5 list calls 0x332 BRAKE_GEAR_INFO_ID). Pulsed, not a single held pedal; unconfirmed."),
    0x340: ("Vehicle speed / gear candidate on Jeep IHS. This log is mostly 0xFF (parked / not populated).",
            "known ID (JL forum); no usable speed bytes in this parked capture"),
    0x3B2: ("Lighting-status flags, byte 0: 0x03 idle, 0x83 lights on (59.48-61.71 s), "
            "0x13 with one turn-signal (72.54-73.56 s), 0x0B with the other (74.54-76.86 s).",
            "educated guess (JL notes list 0x3B2 as lighting / dimmer / door / lock status; flags line up with 0x318 and 0x2FA)"),
    0x214: ("Same slowly-varying analog as CAN-C 0x214; dips at crank. Not a discrete light switch.",
            "educated guess only; not decoded further"),
    0x3D2: ("Odometer candidate on some FCA IHS lists. Not decoded here.",
            "known ID only (JL / magikh0e candidate); payload not interpreted"),
}


def load_log(path: Path) -> pd.DataFrame:
    df = pd.read_csv(path, index_col=False)
    df = df.loc[:, ~df.columns.astype(str).str.startswith("Unnamed")]
    ts = df["Time Stamp"].astype("int64")
    df["t"] = (ts - int(ts.iloc[0])) / 1.0e6
    df["can_id"] = [int(str(x), 16) for x in df["ID"]]
    df["dlc"] = df["LEN"].astype(int)
    for i, col in enumerate(["D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8"]):
        df[f"b{i}"] = [
            int(str(x), 16) if pd.notna(x) and str(x) not in ("", "nan") else 0
            for x in df[col]
        ]
    return df


def arr(series) -> np.ndarray:
    """Integer view of CAN data bytes (not timestamps)."""
    return np.asarray(series, dtype=np.int64)


def tarr(series) -> np.ndarray:
    return np.asarray(series, dtype=np.float64)


def be16(hi, lo) -> np.ndarray:
    return (arr(hi) << 8) | arr(lo)


def id_table(df: pd.DataFrame, guesses: dict) -> pd.DataFrame:
    rows = []
    for cid, g in df.groupby("can_id", sort=True):
        dlc = int(g.dlc.mode().iloc[0])
        changing, constant = [], []
        for b in range(dlc):
            col = g[f"b{b}"]
            n = int(col.nunique())
            if n <= 1:
                constant.append(f"b{b}=0x{int(col.iloc[0]):02X}")
            else:
                changing.append(f"b{b}({n})")
        if len(g) > 1:
            dt = np.diff(tarr(g.t))
            dt = dt[dt > 1e-6]
            period = float(np.median(dt) * 1000.0) if len(dt) else np.nan
        else:
            period = np.nan
        fn, conf = guesses.get(int(cid), ("unknown", "unknown — no public match claimed"))
        rows.append(
            {
                "id_hex": f"0x{cid:03X}",
                "id_dec": int(cid),
                "count": int(len(g)),
                "period_ms_median": None if np.isnan(period) else round(period, 2),
                "rate_hz": None if np.isnan(period) or period <= 0 else round(1000.0 / period, 2),
                "dlc": dlc,
                "bytes_changing": " ".join(changing) if changing else "",
                "bytes_constant": " ".join(constant) if constant else "",
                "function_guess": fn,
                "confidence": conf,
            }
        )
    return pd.DataFrame(rows)


def write_signals_canc(df: pd.DataFrame, path: Path) -> None:
    m = df[df.can_id == 0x108]
    th = df[df.can_id == 0x120]
    br = df[df.can_id == 0x11C]
    st = df[df.can_id == 0x140]
    lv = df[df.can_id == 0x318]
    bcm = df[df.can_id == 0x334]
    parts = [
        pd.DataFrame({"t_s": m.t.to_numpy(), "rpm_0x108": be16(m.b0, m.b1)}),
        pd.DataFrame({"t_s": th.t.to_numpy(), "aps_pct_0x120": arr(th.b2) * 0.4}),
        pd.DataFrame(
            {
                "t_s": br.t.to_numpy(),
                "speed_kmh_0x11C": be16(br.b4, br.b5) * 0.0078125,
                "brake_pedal_raw_0x11C": ((arr(br.b2) & 0x0F) << 8) | arr(br.b3),
            }
        ),
        pd.DataFrame(
            {
                "t_s": st.t.to_numpy(),
                "brake_pedal_state_0x140": (arr(st.b0) >> 2) & 0x3,
            }
        ),
        pd.DataFrame(
            {
                "t_s": lv.t.to_numpy(),
                "stalk_b0_0x318": arr(lv.b0),
                "wiper_b2_0x318": arr(lv.b2),
            }
        ),
        pd.DataFrame(
            {
                "t_s": bcm.t.to_numpy(),
                "dimmer_b6_0x334": arr(bcm.b6),
                "bcm_b7_0x334": arr(bcm.b7),
                "turn_lamps_b3_0x334": arr(bcm.b3),
            }
        ),
    ]
    # outer-merge on a 20 ms grid for a compact CSV
    t0, t1 = float(df.t.min()), float(df.t.max())
    grid = pd.DataFrame({"t_s": np.round(np.arange(t0, t1 + 0.02, 0.05), 3)})
    out = grid
    for p in parts:
        p = p.copy()
        p["t_s"] = np.round(p["t_s"].to_numpy(), 3)
        out = pd.merge_asof(out, p.sort_values("t_s"), on="t_s", direction="nearest", tolerance=0.05)
    out.to_csv(path, index=False)


def write_signals_ihs(df: pd.DataFrame, path: Path) -> None:
    r = df[df.can_id == 0x322]
    b = df[df.can_id == 0x2C2]
    ign = df[df.can_id == 0x122]
    dim = df[df.can_id == 0x2FA]
    lit = df[df.can_id == 0x3B2]
    lv = df[df.can_id == 0x318]
    rpm = be16(r.b0, r.b1)
    parts = [
        pd.DataFrame(
            {
                "t_s": r.t.to_numpy(),
                "rpm_0x322": np.where(rpm == 0xFFFF, 0, rpm),
                "speed_raw_0x322": be16(r.b2, r.b3),
            }
        ),
        pd.DataFrame({"t_s": b.t.to_numpy(), "battery_V_0x2C2": arr(b.b2) / 10.0}),
        pd.DataFrame({"t_s": ign.t.to_numpy(), "ign_b0_0x122": arr(ign.b0)}),
        pd.DataFrame(
            {
                "t_s": dim.t.to_numpy(),
                "dimmer_b2_0x2FA": arr(dim.b2),
                "dimmer_b1_0x2FA": arr(dim.b1),
            }
        ),
        pd.DataFrame({"t_s": lit.t.to_numpy(), "lights_b0_0x3B2": arr(lit.b0)}),
        pd.DataFrame(
            {
                "t_s": lv.t.to_numpy(),
                "stalk_b0_0x318": arr(lv.b0),
                "wiper_b2_0x318": arr(lv.b2),
            }
        ),
    ]
    t0, t1 = float(df.t.min()), float(df.t.max())
    out = pd.DataFrame({"t_s": np.round(np.arange(t0, t1 + 0.05, 0.05), 3)})
    for p in parts:
        p = p.copy()
        p["t_s"] = np.round(p["t_s"].to_numpy(), 3)
        out = pd.merge_asof(out, p.sort_values("t_s"), on="t_s", direction="nearest", tolerance=0.08)
    out.to_csv(path, index=False)


def style_ax(ax, ylabel, title=None):
    ax.set_ylabel(ylabel)
    ax.grid(True, alpha=0.35)
    if title:
        ax.set_title(title)
    ax.legend(loc="upper left", framealpha=0.9)


def plot_canc(df: pd.DataFrame) -> Path:
    fig, axes = plt.subplots(4, 1, figsize=(12.5, 11.5), sharex=True)
    m = df[df.can_id == 0x108]
    axes[0].plot(m.t, be16(m.b0, m.b1), color=BLUE, lw=1.2, label="0x108 ENGINE_RPM (known, opendbc)")
    style_ax(axes[0], "RPM")

    th = df[df.can_id == 0x120]
    axes[1].plot(th.t, arr(th.b2) * 0.4, color=ORANGE, lw=1.2, label="0x120 byte2 * 0.4 % (known, opendbc)")
    style_ax(axes[1], "Throttle %")

    br = df[df.can_id == 0x11C]
    st = df[df.can_id == 0x140]
    axes[2].plot(
        br.t,
        ((arr(br.b2) & 0x0F) << 8) | arr(br.b3),
        color=BLUE,
        lw=1.2,
        label="0x11C BRAKE_PEDAL raw (known, opendbc)",
    )
    axes[2].plot(
        st.t,
        ((arr(st.b0) >> 2) & 0x3) * 800,
        color=ORANGE,
        lw=1.4,
        ls="--",
        label="0x140 Brake_Pedal_State * 800 (known, opendbc)",
    )
    style_ax(axes[2], "Brake")

    lv = df[df.can_id == 0x318]
    bcm = df[df.can_id == 0x334]
    axes[3].step(lv.t, arr(lv.b0), where="post", color=BLUE, lw=1.4, label="0x318 byte0 stalk (0x01/0x02 turn, 0x04 high beam)")
    axes[3].step(lv.t, arr(lv.b2), where="post", color=ORANGE, lw=1.2, ls="--", label="0x318 byte2 wiper steps (educated guess)")
    axes[3].step(bcm.t, arr(bcm.b6) / 10.0, where="post", color=GOLD, lw=1.6, label="0x334 byte6 dimmer/illum / 10 (0xC8=20) educated guess")
    style_ax(axes[3], "Stalk / dimmer")
    axes[3].set_xlabel("Seconds from start of pins_6-14.csv (CAN-C)")

    fig.suptitle("2017 Ram 3500  —  CAN-C (OBD pins 6/14)  —  RPM, throttle, brake, stalk, dimmer", fontsize=12)
    fig.tight_layout()
    path = PLOTS / "pins_6-14_rpm_throttle_brake.png"
    fig.savefig(path, dpi=120)
    plt.close(fig)
    return path


def plot_canc_lights(df: pd.DataFrame) -> Path:
    fig, axes = plt.subplots(3, 1, figsize=(12.5, 9.0), sharex=True)
    bcm = df[df.can_id == 0x334]
    lv = df[df.can_id == 0x318]
    axes[0].step(bcm.t, arr(bcm.b6), where="post", color=BLUE, lw=2.0, label="0x334 byte6  (0=off, 200=0xC8 on)  dimmer/illumination candidate")
    axes[0].set_yticks([0, 34, 200])
    axes[0].set_yticklabels(["0 off", "0x22 Jeep min (not seen)", "0xC8 Jeep max"])
    style_ax(axes[0], "Byte 6")

    axes[1].step(bcm.t, arr(bcm.b7), where="post", color=ORANGE, lw=1.6, label="0x334 byte7  (bit2 high-beam display at 92.17-94.33 s)")
    axes[1].step(lv.t, arr(lv.b0), where="post", color=BLUE, lw=1.3, ls="--", label="0x318 byte0 stalk  (0x04 high beam, 0x01/0x02 turns)")
    style_ax(axes[1], "High beam / stalk")

    axes[2].step(bcm.t, arr(bcm.b3), where="post", color=GOLD, lw=1.6, label="0x334 byte3  (0x40 and 0x80 blink = turn-lamp outputs)")
    axes[2].step(lv.t, arr(lv.b0), where="post", color=SKY, lw=1.2, ls="--", label="0x318 byte0 stalk request (0x01/0x02 turns, 0x40 unknown pulse)")
    axes[2].set_ylim(-5, 140)
    style_ax(axes[2], "Turn lamps (raw byte)")
    axes[2].set_xlabel("Seconds from start of pins_6-14.csv (CAN-C)")

    fig.suptitle("2017 Ram 3500  —  CAN-C (pins 6/14)  —  dash illumination and lighting", fontsize=12)
    fig.tight_layout()
    path = PLOTS / "pins_6-14_lights_dimmer.png"
    fig.savefig(path, dpi=120)
    plt.close(fig)
    return path


def plot_ihs(df: pd.DataFrame) -> Path:
    fig, axes = plt.subplots(4, 1, figsize=(12.5, 11.5), sharex=True)
    r = df[df.can_id == 0x322]
    rpm = be16(r.b0, r.b1)
    axes[0].plot(r.t, np.where(rpm == 0xFFFF, 0, rpm), color=BLUE, lw=1.2, label="0x322 RPM (known, magikh0e/JL; 0xFFFF plotted as 0)")
    style_ax(axes[0], "RPM")

    b = df[df.can_id == 0x2C2]
    axes[1].plot(b.t, arr(b.b2) / 10.0, color=ORANGE, lw=1.3, label="0x2C2 byte2 / 10 V (known, magikh0e)")
    style_ax(axes[1], "Battery V")

    dim = df[df.can_id == 0x2FA]
    lit = df[df.can_id == 0x3B2]
    axes[2].step(dim.t, arr(dim.b2), where="post", color=BLUE, lw=2.0, label="0x2FA byte2 dimmer/illum (0 / 0xC8) educated guess")
    axes[2].step(lit.t, arr(lit.b0), where="post", color=ORANGE, lw=1.3, ls="--", label="0x3B2 byte0 lighting flags (educated guess)")
    style_ax(axes[2], "Lights / dimmer")

    lv = df[df.can_id == 0x318]
    ign = df[df.can_id == 0x122]
    axes[3].step(lv.t, arr(lv.b0), where="post", color=BLUE, lw=1.4, label="0x318 byte0 stalk (turns / 0x40 pulse)")
    axes[3].step(lv.t, arr(lv.b2), where="post", color=ORANGE, lw=1.2, ls="--", label="0x318 byte2 wiper steps (educated guess)")
    axes[3].step(ign.t, arr(ign.b0), where="post", color=GOLD, lw=1.2, ls=":", label="0x122 byte0 ignition (known ID)")
    style_ax(axes[3], "Stalk / ignition")
    axes[3].set_xlabel("Seconds from start of pins_3-11.csv (CAN-IHS)")

    fig.suptitle("2017 Ram 3500  —  CAN-IHS (OBD pins 3/11)  —  RPM, battery, dimmer, stalk", fontsize=12)
    fig.tight_layout()
    path = PLOTS / "pins_3-11_rpm_battery_dimmer.png"
    fig.savefig(path, dpi=120)
    plt.close(fig)
    return path


CURATED_CANC = [
    # t_start, t_end, id, signal, values, what, confidence
    (0.00, 63.75, "0x108", "ENGINE_RPM", "0", "Key on, engine off", "known (opendbc; RPM=0)"),
    (63.75, 64.50, "0x108", "ENGINE_RPM", "0 to 942", "Engine crank / start", "known (opendbc)"),
    (64.50, 108.00, "0x108", "ENGINE_RPM", "~690-710", "Idle", "known (opendbc)"),
    (90.01, 95.42, "0x334", "byte6 illumination", "00 then C8 then 00", "Headlights / park lights on, then off. Dash illumination candidate at 0xC8.", "educated guess (0xC8 = Jeep $291 dimmer max; coincident with 0x334 high-beam / stalk)"),
    (92.17, 94.33, "0x334 / 0x318", "high beam", "0x334 b7 has bit2; 0x318 byte0=0x04", "High beam on during the lights-on window", "known ID (opendbc HIGH_BEAM_DISPLAY / HIGH_BEAM_PRESSED); verified by coincidence"),
    (97.04, 105.92, "0x318", "byte2", "00-01-02-03-04-05-0D-0E then back to 00", "Wipers cycled through speeds (including 0x0D/0x0E) then off", "educated guess (JL puts wipers in 0x318; one rising/falling burst)"),
    (108.70, 112.90, "0x11C / 0x140", "brake pedal", "0x11C raw 0-1654; 0x140 byte0 0x80->0x85", "Brake pedal pressed and released", "known (opendbc ESP_8 / ESP_1)"),
    (115.12, 115.76, "0x120 / 0x108", "APS / RPM", "APS to 24%", "Light throttle blip", "known (opendbc)"),
    (117.82, 118.58, "0x120 / 0x108", "APS / RPM", "APS to 66%; RPM to 2664", "Larger throttle blip", "known (opendbc)"),
    (124.08, 124.40, "0x318", "byte0", "0x40 pulse", "Unknown stalk or column button (also seen on the IHS log). Not identified.", "unknown"),
    (126.64, 127.52, "0x318", "byte0", "0x04", "Short high-beam flash (stalk 0x04)", "educated guess (same code as the earlier high-beam event)"),
    (128.24, 130.56, "0x318 / 0x334", "turn A", "0x318 byte0=0x01; 0x334 byte3=0x40 blinking", "Turn signal A. Side unconfirmed (JL: 0x01=left; 5th-gen BCM names disagree).", "known as a turn signal; side is an educated guess / unconfirmed"),
    (131.60, 134.64, "0x318 / 0x334", "turn B", "0x318 byte0=0x02; 0x334 byte3=0x80 blinking", "Turn signal B (the other side).", "known as a turn signal; side is an educated guess / unconfirmed"),
]

CURATED_IHS = [
    (0.00, 6.10, "0x122", "byte0", "00, 03, then 04", "Key activity, then key to RUN at 6.10 s (engine still off)", "known ID; byte meaning educated guess from RPM correlation"),
    (6.12, 9.13, "0x1C0", "byte4", "11 -> 51 -> 11", "Tracks the key-RUN transition, not a Jeep-style lock command (byte0 stayed 00)", "known ID; this change is an educated guess as ignition-related, not locks"),
    (36.12, 37.00, "0x122 / 0x322 / 0x2C2", "crank", "0x122=05 then 15; RPM leaves 0 at 36.90 s; battery sags to 10.6 V", "Engine crank / start, then 0x122 returns to 04 with engine running", "known IDs; 0x122 crank codes are an educated guess"),
    (37.00, 59.00, "0x322 / 0x2C2", "idle / charging", "RPM ~700; battery 13.7-14.5 V", "Idle with alternator charging", "known"),
    (59.49, 61.70, "0x2FA / 0x3B2", "lights / dimmer", "0x2FA byte2=0xC8, byte1=0; 0x3B2 byte0=0x83", "Lights on for ~2.2 s, then off. Best IHS dash-illumination candidate.", "educated guess (0xC8 = Jeep dimmer max; flags move together)"),
    (62.84, 63.08, "0x318", "byte0", "0x40 pulse", "Same unknown stalk/button pulse seen on CAN-C", "unknown"),
    (64.12, 71.00, "0x318", "byte2", "02-03-04-05-0D-0E then down to 00", "Wipers on, through speeds, then off", "educated guess (same pattern as CAN-C 0x318 byte2)"),
    (72.52, 73.80, "0x318 / 0x3B2", "turn", "0x318 byte0=0x02; 0x3B2 byte0=0x13", "Turn signal (side unconfirmed; this is the 0x02 stalk code)", "known as a turn signal; side unconfirmed"),
    (74.52, 76.84, "0x318 / 0x3B2", "turn", "0x318 byte0=0x01; 0x3B2 byte0=0x0B", "The other turn signal (0x01 stalk code)", "known as a turn signal; side unconfirmed"),
    (78.06, 82.75, "0x332", "byte2", "0x18 <-> 0x58 pulses", "Possible brake-switch pulses (ID is a JL brake/gear message). Not a single held press.", "educated guess; unconfirmed"),
    (85.00, 90.50, "0x322", "RPM", "blip to 2411, then back to ~700", "Throttle blip (no IHS throttle byte was confirmed; RPM is known)", "known (0x322 RPM)"),
]


def curated_frame(rows, log_name):
    return pd.DataFrame(
        [
            {
                "log": log_name,
                "t_start_s": a,
                "t_end_s": b,
                "id": c,
                "signal": d,
                "observed": e,
                "interpretation": f,
                "confidence": g,
            }
            for a, b, c, d, e, f, g in rows
        ]
    )


def main() -> None:
    canc = load_log(LOGS / "pins_6-14.csv")
    ihs = load_log(LOGS / "pins_3-11.csv")

    t_c = id_table(canc, CANC_GUESS)
    t_i = id_table(ihs, IHSS_GUESS)
    t_c.to_csv(HERE / "id_table_pins_6-14.csv", index=False)
    t_i.to_csv(HERE / "id_table_pins_3-11.csv", index=False)

    curated_frame(CURATED_CANC, "pins_6-14.csv").to_csv(HERE / "events_pins_6-14.csv", index=False)
    curated_frame(CURATED_IHS, "pins_3-11.csv").to_csv(HERE / "events_pins_3-11.csv", index=False)

    write_signals_canc(canc, HERE / "signals_pins_6-14.csv")
    write_signals_ihs(ihs, HERE / "signals_pins_3-11.csv")

    p1 = plot_canc(canc)
    p2 = plot_canc_lights(canc)
    p3 = plot_ihs(ihs)

    # overlap / rate summary to stdout
    def brief(name, df):
        dur = float(df.t.max())
        print(f"{name}: {len(df)} frames, {dur:.3f} s, {df.can_id.nunique()} IDs, {len(df)/dur:.1f} fps")

    brief("pins_6-14 (raw_942e)", canc)
    brief("pins_3-11 (raw_6d05)", ihs)
    print("plots:", p1.name, p2.name, p3.name)


if __name__ == "__main__":
    main()
