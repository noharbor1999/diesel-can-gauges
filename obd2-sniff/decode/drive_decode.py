#!/usr/bin/env python3
"""2017 Ram 3500 CAN-C drive log (sorted timestamps) — boost, coasts, temps."""
from __future__ import annotations
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

HERE = Path(__file__).resolve().parent
LOGS = HERE.parent / 'data' / 'logs'
PLOTS = HERE / 'plots'
PLOTS.mkdir(exist_ok=True)
BLUE, ORANGE, GOLD, SKY = '#0072B2', '#E69F00', '#CC9A06', '#56B4E9'

def arr(s):
    return np.asarray(s, dtype=np.int64)

def be16(h, l):
    return (arr(h) << 8) | arr(l)

def load(path: Path) -> pd.DataFrame:
    df = pd.read_csv(path, index_col=False)
    df = df.loc[:, ~df.columns.astype(str).str.startswith('Unnamed')]
    df['can_id'] = [int(str(x), 16) for x in df['ID']]
    for i, col in enumerate(['D1','D2','D3','D4','D5','D6','D7','D8']):
        df[f'b{i}'] = [int(str(x), 16) if pd.notna(x) and str(x) not in ('','nan') else 0 for x in df[col]]
    # This file is not in timestamp order: first 164 rows belong at t≈77.28 s
    # and interleave with the in-order stream, causing brief flag chatter.
    # Drop them; the rest of that 80 ms window is still present.
    df = df.iloc[164:].copy()
    df = df.sort_values('Time Stamp', kind='mergesort').reset_index(drop=True)
    ts = df['Time Stamp'].astype('int64')
    # Keep t=0 at the true earliest remaining timestamp (same origin as the full file).
    df['t'] = (ts - int(ts.min())) / 1e6
    return df

def of(df, cid):
    return df[df.can_id == cid]

def ip(t, y, g):
    t = np.asarray(t, float); y = np.asarray(y, float)
    return np.interp(g, t, y, left=y[0], right=y[-1])

def style(ax, ylabel, title=None):
    ax.set_ylabel(ylabel)
    ax.grid(True, alpha=0.35)
    if title:
        ax.set_title(title)
    ax.legend(loc='upper left', framealpha=0.9, fontsize=8)

BOOSTS = [
    (24.90, 31.90, 'boost1'),
    (51.80, 58.60, 'boost2'),
    (97.50, 99.60, 'boost3'),
    (132.20, 143.00, 'boost4'),
    (161.40, 164.50, 'boost5'),
]
COASTS = [
    (32.00, 51.15, 'coast1'),
    (58.70, 77.30, 'coast2'),
    (99.70, 104.40, 'coast3'),
    (143.15, 159.10, 'coast4'),
    (164.45, 168.50, 'coast5'),
    (171.15, 179.80, 'coast6'),
]
TOWHAUL = 34.64

EVENTS = [
    (0.00, 2.95, 'bus', 'idle, stopped', 'RPM ~700, speed 0', 'Log starts key-on, engine running, stopped.', 'confirmed by script'),
    (2.95, 15.35, '0x11C / 0x120', 'roll-out', 'speed to ~26 km/h, light APS', 'First roll-out.', 'confirmed by script'),
    (15.35, 19.40, '0x11C / 0x140', 'service brake', 'pedal raw up to 1484; speed 23.5->5.1', 'Service-brake stop, not exhaust brake.', 'confirmed by script; known (opendbc)'),
    (24.90, 31.90, '0x120 / 0x108 / 0x30A', 'boost 1', 'APS to ~74%; RPM to ~2600; 0x30A b6 max 204 at 26.51', 'First hard pull. 204 kPa-guess ≈ 29.6 psi.', 'confirmed by script; scale educated guess'),
    (32.00, 51.15, '0x120 / 0x11C / 0x30A', 'exhaust-brake coast 1', 'APS=0, pedal=0, 89->44 km/h in 19.2 s (2.37 km/h/s); 0x30A b7 comes on after bleed-down', 'No-pedal coast. Tow/Haul candidate flips at 34.64 during this coast.', 'confirmed by script (coast); Tow/Haul educated guess'),
    (34.64, 34.64, '0x170', 'byte 1 bit 1', '252->254, stays 254 to end of log', 'Best Tow/Haul mode-flag candidate. This bit was 0 (byte=252) for all of run 2 while the engine was running.', 'educated guess (one-shot mid-drive, stays on, off in run2)'),
    (51.80, 58.60, '0x120 / 0x108 / 0x30A', 'boost 2', 'APS to ~80%; 0x30A b6 max 242 at 55.41 (highest in the log)', 'Hardest pull. 242 kPa-guess ≈ 35.1 psi (top of the 25-35 psi band).', 'confirmed by script; scale educated guess'),
    (58.70, 77.30, '0x120 / 0x11C / 0x30A', 'exhaust-brake coast 2', 'APS=0, pedal=0, 101->44 km/h in 18.6 s (3.11 km/h/s); 0x30A b7 med ~46; locked-ratio downshifts', 'Stronger no-pedal coast after the Tow/Haul candidate. TCC-locked gear plateaus.', 'confirmed by script (coast)'),
    (97.50, 99.60, '0x120 / 0x30A', 'boost 3', 'short pull, 0x30A b6 max 179 at 98.81', 'Shorter boost.', 'confirmed by script'),
    (99.70, 104.40, '0x120 / 0x11C', 'coast 3 (weaker)', '4.7 s, 51->47 km/h (1.01 km/h/s); 0x30A b7=0', 'No-pedal but weak decel; exhaust-brake effort looks low or not engaged.', 'confirmed by script (coast)'),
    (104.45, 117.00, '0x11C / 0x140', 'service brake to a stop', 'pedal used; speed to 0 by ~114 s, idle through ~117', 'Service brake, then stopped.', 'confirmed by script; known (opendbc)'),
    (132.20, 143.00, '0x120 / 0x108 / 0x30A', 'boost 4', 'APS>=60% for 10.8 s; speed to 107.5 km/h (66.8 mph); 0x30A b6 max 205 at 139.41', 'Longest pull, peak speed of the log.', 'confirmed by script'),
    (143.15, 159.10, '0x120 / 0x11C / 0x30A', 'exhaust-brake coast 4', '16.0 s, 107->67 km/h (2.52 km/h/s); 0x30A b7 on between downshifts', 'No-pedal coast with locked 6th then downshifts to 5th.', 'confirmed by script (coast)'),
    (161.40, 164.50, '0x120 / 0x30A', 'boost 5', '0x30A b6 max 187 at 162.52', 'Short pull.', 'confirmed by script'),
    (164.45, 168.50, '0x120 / 0x11C', 'exhaust-brake coast 5', '4.0 s, 95->85 km/h (2.49 km/h/s); 0x30A b7 on', 'No-pedal coast, then service brake 168.6-171.1.', 'confirmed by script (coast)'),
    (171.15, 179.80, '0x120 / 0x11C / 0x2A8', 'exhaust-brake coast 6', '8.7 s, 67->35 km/h (3.77 km/h/s); RPM to 2510; 0x2A8 b2 downshifts 5th->4th->3rd; 0x30A b7 max 81', 'Strongest no-pedal decel, deep downshifts (Tow/Haul-like).', 'confirmed by script (coast)'),
    (194.15, 208.00, '0x11C / 0x140', 'service brake to idle', 'speed 0 from 205.35; RPM ~700 to end of log', 'End of drive, key still on, engine idling.', 'confirmed by script'),
]

def main():
    d = load(LOGS / 'pins_6-14_drive.csv')
    print('sorted', len(d), f'{d.t.max():.3f}s', d.can_id.nunique(), 'IDs')
    pd.DataFrame([{
        'log': 'pins_6-14_drive.csv (timestamps sorted)',
        't_start_s': a, 't_end_s': b, 'id': c, 'signal': s,
        'observed': o, 'interpretation': i, 'confidence': k,
    } for a,b,c,s,o,i,k in EVENTS]).to_csv(HERE / 'events_pins_6-14_drive.csv', index=False)

    grid = np.arange(0, float(d.t.max()), 0.05)
    m108, m11c, m120 = of(d, 0x108), of(d, 0x11C), of(d, 0x120)
    rpm = ip(m108.t, be16(m108.b0, m108.b1), grid)
    spd = ip(m11c.t, be16(m11c.b4, m11c.b5) * 0.0078125, grid)
    ped = ip(m11c.t, ((arr(m11c.b2) & 0x0F) << 8) | arr(m11c.b3), grid)
    aps = ip(m120.t, arr(m120.b2) * 0.4, grid)
    m30 = of(d, 0x30A)
    b6 = ip(m30.t, arr(m30.b6), grid)
    b7 = ip(m30.t, arr(m30.b7), grid)
    m2a = of(d, 0x2A0)
    vgt = ip(m2a.t, arr(m2a.b2), grid)
    m2a8 = of(d, 0x2A8)
    gear = ip(m2a8.t, arr(m2a8.b2), grid)
    m170 = of(d, 0x170)
    th = ip(m170.t, ((arr(m170.b1) >> 1) & 1) * 1.0, grid)

    # --- boost plot ---
    fig, ax = plt.subplots(4, 1, figsize=(13.0, 10.2), sharex=True)
    ax[0].plot(grid, rpm, color=BLUE, lw=1.1, label='0x108 ENGINE_RPM (known, opendbc)')
    style(ax[0], 'RPM')
    ax[1].plot(grid, aps, color=ORANGE, lw=1.1, label='0x120 byte2 * 0.4 % (known, opendbc)')
    style(ax[1], 'Throttle %')
    ax[2].plot(grid, spd, color=SKY, lw=1.1, label='0x11C speed km/h (known, opendbc)')
    ax[2].plot(grid, spd / 1.60934, color=GOLD, lw=0.9, ls='--', label='same, mph')
    style(ax[2], 'Speed')
    ax[3].plot(grid, b6, color=GOLD, lw=1.5, label='0x30A byte6  boost/MAP candidate, raw (educated guess, 1≈1 kPa gauge)')
    ax[3].plot(grid, vgt, color=SKY, lw=1.0, ls='--', label='0x2A0 byte2  load / possible VGT (educated guess)')
    for a, b, _ in BOOSTS:
        for axx in ax:
            axx.axvspan(a, b, color=ORANGE, alpha=0.12)
    style(ax[3], 'Raw byte')
    ax[3].set_xlabel('Seconds from start of pins_6-14_drive.csv (CAN-C), timestamps SORTED.  Shaded: hard-throttle boost pulls.')
    fig.suptitle('Drive log — CAN-C RPM, throttle, speed, and boost/MAP candidates', fontsize=12)
    fig.tight_layout()
    p1 = PLOTS / 'drive_boost.png'
    fig.savefig(p1, dpi=120)
    plt.close(fig)

    # --- exhaust-brake plot ---
    fig, ax = plt.subplots(4, 1, figsize=(13.0, 10.4), sharex=True)
    ax[0].plot(grid, spd, color=SKY, lw=1.2, label='0x11C speed km/h (known, opendbc)')
    ax[0].plot(grid, aps * 1.2, color=ORANGE, lw=1.0, label='0x120 APS %  (scaled ×1.2 for overlay only)')
    ax[0].plot(grid, np.clip(ped / 20.0, 0, 80), color=BLUE, lw=0.9, ls='--', label='0x11C brake pedal raw / 20 (service brake)')
    style(ax[0], 'km/h / % / raw')
    ax[1].plot(grid, b7, color=GOLD, lw=1.5, label='0x30A byte7  coast-only analog (educated guess: exhaust-brake effort / backpressure)')
    ax[1].plot(grid, b6, color=SKY, lw=0.9, ls='--', label='0x30A byte6  boost candidate (for contrast)')
    style(ax[1], 'Raw byte')
    ax[2].plot(grid, gear, color=BLUE, lw=1.3, label='0x2A8 byte2  gear candidate, steps of 17 (17=1st … 102=6th)  educated guess')
    ax[2].set_yticks([0, 17, 34, 51, 68, 85, 102])
    ax[2].set_yticklabels(['0','17 1st?','34 2nd?','51 3rd?','68 4th?','85 5th?','102 6th?'])
    style(ax[2], '0x2A8 b2')
    ax[3].step(grid, th, where='post', color=ORANGE, lw=1.8, label='0x170 byte1 bit1  Tow/Haul mode candidate (0=off/252, 1=on/254)  educated guess')
    ax[3].axvline(TOWHAUL, color=GOLD, lw=1.2, ls=':', label=f'Tow/Haul candidate on at {TOWHAUL:.2f} s')
    ax[3].set_ylim(-0.15, 1.3)
    style(ax[3], 'Flag')
    for a, b, _ in COASTS:
        for axx in ax:
            axx.axvspan(a, b, color=GOLD, alpha=0.10)
    ax[3].set_xlabel('Seconds from start (sorted).  Shaded gold: no-pedal coasts (APS≈0, speed>15, service brake not used).')
    fig.suptitle('Drive log — exhaust-brake coasts, 0x30A byte7, gear, and Tow/Haul candidate', fontsize=12)
    fig.tight_layout()
    p2 = PLOTS / 'drive_exhaust_brake.png'
    fig.savefig(p2, dpi=120)
    plt.close(fig)
    print('plots', p1.name, p2.name)

if __name__ == '__main__':
    main()
