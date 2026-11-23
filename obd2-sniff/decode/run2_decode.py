#!/usr/bin/env python3
"""Run 2 of 2017 Ram 3500 sniff: timed script, one bus at a time."""
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
BLUE, ORANGE, GOLD, SKY, BLACK = '#0072B2', '#E69F00', '#CC9A06', '#56B4E9', '#000000'

def load(path: Path) -> pd.DataFrame:
    df = pd.read_csv(path, index_col=False)
    df = df.loc[:, ~df.columns.astype(str).str.startswith('Unnamed')]
    ts = df['Time Stamp'].astype('int64')
    df['t'] = (ts - int(ts.iloc[0])) / 1e6
    df['can_id'] = [int(str(x), 16) for x in df['ID']]
    df['dlc'] = df['LEN'].astype(int)
    for i, col in enumerate(['D1','D2','D3','D4','D5','D6','D7','D8']):
        df[f'b{i}'] = [int(str(x), 16) if pd.notna(x) and str(x) not in ('','nan') else 0 for x in df[col]]
    return df

def arr(s):
    return np.asarray(s, dtype=np.int64)

def be16(h, l):
    return (arr(h) << 8) | arr(l)

def style(ax, ylabel, title=None):
    ax.set_ylabel(ylabel)
    ax.grid(True, alpha=0.35)
    if title:
        ax.set_title(title)
    ax.legend(loc='upper left', framealpha=0.9)

def plot_dimmer(c, i) -> Path:
    fig, ax = plt.subplots(2, 1, figsize=(12.5, 8.4))
    r = i[i.can_id == 0x2FA]
    ax[0].step(r.t, arr(r.b2), where='post', color=BLUE, lw=2.0, label='0x2FA byte2 illumination (0 / 0xC8)')
    ax[0].step(r.t, arr(r.b4), where='post', color=ORANGE, lw=1.4, ls='--', label='0x2FA byte4 (brief 0xC8 at 79.59-80.77 s)')
    ax[0].step(r.t, arr(r.b6), where='post', color=GOLD, lw=1.2, ls=':', label='0x2FA byte6 (tracks byte4 in this log)')
    lit = i[i.can_id == 0x3B2]
    ax[0].step(lit.t, arr(lit.b0), where='post', color=SKY, lw=1.0, alpha=0.8, label='0x3B2 byte0 lighting flags (0x83 = lights on)')
    ax[0].axvspan(49.36, 59.43, color=SKY, alpha=0.12)
    ax[0].axvspan(69, 90, color=ORANGE, alpha=0.08)
    ax[0].set_xlim(40, 100)
    ax[0].set_xlabel('Seconds from start of pins_3-11_run2.csv (CAN-IHS)')
    style(ax[0], 'Raw byte', 'CAN-IHS — headlights then dimmer step (headlights were OFF in the dimmer window)')

    r = c[c.can_id == 0x334]
    ax[1].step(r.t, arr(r.b6), where='post', color=BLUE, lw=2.0, label='0x334 byte6 illumination (0 / 0xC8)')
    ax[1].step(r.t, arr(r.b7), where='post', color=ORANGE, lw=1.3, ls='--', label='0x334 byte7 (0x02 while headlights on)')
    ax[1].axvspan(55.82, 68.24, color=SKY, alpha=0.12)
    ax[1].axvspan(78, 103, color=ORANGE, alpha=0.08)
    ax[1].set_xlim(45, 110)
    ax[1].set_xlabel('Seconds from start of pins_6-14_run2.csv (CAN-C)  —  separate session')
    style(ax[1], 'Raw byte', 'CAN-C — 0x334 byte6 stays 0 after headlights off; no dimmer-wheel sweep recorded')
    fig.suptitle('Run 2 — dash dimmer / illumination  (shaded: lights on, then expected dimmer window)', fontsize=12)
    fig.tight_layout()
    path = PLOTS / 'run2_dimmer.png'
    fig.savefig(path, dpi=120)
    plt.close(fig)
    return path

def plot_locks_turns(c, i) -> Path:
    fig, ax = plt.subplots(2, 2, figsize=(13.2, 8.2))
    # IHS locks
    r = i[i.can_id == 0x2FA]
    ax[0,0].step(r.t, arr(r.b0), where='post', color=BLUE, lw=2.0, label='0x2FA byte0  (0x40 = locked)')
    ax[0,0].step(r.t, arr(r.b3), where='post', color=ORANGE, lw=1.3, ls='--', label='0x2FA byte3  (0x18 idle, 0x00 locked)')
    e = i[i.can_id == 0x2E2]
    if len(e):
        ax[0,0].step(e.t, arr(e.b0) * 10, where='post', color=GOLD, lw=1.2, ls=':', label='0x2E2 byte0 * 10 (0x04 lock pulse, 0x02 unlock pulse)')
    ax[0,0].set_xlim(15, 50)
    ax[0,0].set_xlabel('s from pins_3-11_run2.csv')
    style(ax[0,0], 'Raw byte', 'IHS locks  (lock 26.05, unlock 38.07)')

    # CAN-C locks
    r = c[c.can_id == 0x334]
    ax[0,1].step(r.t, arr(r.b0), where='post', color=BLUE, lw=2.0, label='0x334 byte0  (0x40 unlocked, 0x00 locked)')
    ax[0,1].set_xlim(20, 55)
    ax[0,1].set_xlabel('s from pins_6-14_run2.csv  (separate session)')
    style(ax[0,1], 'Raw byte', 'CAN-C locks  (lock 29.53, unlock 42.88)')

    # IHS turns
    lv = i[i.can_id == 0x318]
    lit = i[i.can_id == 0x3B2]
    ax[1,0].step(lv.t, arr(lv.b0), where='post', color=BLUE, lw=2.0, label='0x318 byte0 stalk  (0x01 left, 0x02 right)')
    ax[1,0].step(lit.t, arr(lit.b0), where='post', color=ORANGE, lw=1.3, ls='--', label='0x3B2 byte0 lamps  (0x0B left blink, 0x13 right blink)')
    ax[1,0].set_xlim(120, 145)
    ax[1,0].set_xlabel('s from pins_3-11_run2.csv')
    style(ax[1,0], 'Raw byte', 'IHS turns  (left 128.45-133.87, right 135.25-140.37)')

    # CAN-C turns
    lv = c[c.can_id == 0x318]
    bcm = c[c.can_id == 0x334]
    ax[1,1].step(lv.t, arr(lv.b0), where='post', color=BLUE, lw=2.0, label='0x318 byte0 stalk  (0x01 left only in this session)')
    ax[1,1].step(bcm.t, arr(bcm.b3), where='post', color=ORANGE, lw=1.3, ls='--', label='0x334 byte3 lamps  (0x40 left blink; 0x80 right not seen)')
    ax[1,1].set_xlim(135, 160)
    ax[1,1].set_xlabel('s from pins_6-14_run2.csv  (separate session)')
    style(ax[1,1], 'Raw byte', 'CAN-C turns  (left 143.15-149.47; right not logged)')

    fig.suptitle('Run 2 — door locks and turn signals  (two separate script runs, clocks are not shared)', fontsize=12)
    fig.tight_layout()
    path = PLOTS / 'run2_locks_turns.png'
    fig.savefig(path, dpi=120)
    plt.close(fig)
    return path

def plot_boost(c) -> Path:
    fig, ax = plt.subplots(3, 1, figsize=(12.5, 9.0), sharex=True)
    m = c[c.can_id == 0x108]
    ax[0].plot(m.t, be16(m.b0, m.b1), color=BLUE, lw=1.2, label='0x108 ENGINE_RPM (known, opendbc)')
    style(ax[0], 'RPM')
    th = c[c.can_id == 0x120]
    ax[1].plot(th.t, arr(th.b2) * 0.4, color=ORANGE, lw=1.2, label='0x120 byte2 * 0.4 % (known, opendbc)')
    style(ax[1], 'Throttle %')
    b = c[c.can_id == 0x30A]
    ax[2].plot(b.t, arr(b.b6), color=GOLD, lw=1.6, label='0x30A byte6  boost/MAP candidate, raw counts (educated guess)')
    v = c[c.can_id == 0x2A0]
    ax[2].plot(v.t, arr(v.b2), color=SKY, lw=1.1, ls='--', label='0x2A0 byte2  load / possible VGT-related (educated guess)')
    ax[2].axvspan(189.45, 195.59, color=ORANGE, alpha=0.12)
    ax[2].axvspan(202.65, 210.51, color=ORANGE, alpha=0.12)
    style(ax[2], 'Raw byte')
    ax[2].set_xlabel('Seconds from start of pins_6-14_run2.csv (CAN-C).  Shaded: throttle blips (~2000 rpm, no load).')
    ax[0].set_xlim(150, 220)
    fig.suptitle('Run 2 — CAN-C RPM, throttle, and boost/MAP candidates  (no-load revs; boost will be small)', fontsize=12)
    fig.tight_layout()
    path = PLOTS / 'run2_boost.png'
    fig.savefig(path, dpi=120)
    plt.close(fig)
    return path

EVENTS_IHS = [
    (0.00, 13.19, '0x122', 'ignition', '00 01, then 00 02 / 03 02, then 04 02 at 13.19', 'Key off, then key ON (engine off)', 'confirmed by script'),
    (13.19, 26.05, '0x122', 'ignition', '04 02', 'Key ON, engine off (step 2)', 'confirmed by script'),
    (26.05, 38.07, '0x2FA / 0x2E2', 'locks', '0x2FA b0 00->40 at 26.05, 40->00 at 38.07; 0x2E2 b0 pulse 04 then 02', 'Lock doors, wait, unlock. 0x1C0 did not carry a lock command.', 'confirmed by script'),
    (49.36, 59.43, '0x2FA / 0x3B2', 'headlights', '0x2FA b2=C8, 0x3B2 b0=83 for ~10 s, then off', 'Headlights on 10 s, then off. Illumination output is 0xC8 only while lights are on.', 'confirmed by script'),
    (79.59, 80.79, '0x2FA', 'dimmer bytes 4 and 6', 'b2 stayed 00; b4 and b6 00->C8->00 in 1.2 s, no intermediate steps', 'In the dimmer-step window, but headlights were off so b2 (illumination) stayed 0. Not a proportional sweep.', 'confirmed by script (event exists); not a proven linear dimmer level'),
    (89.65, 101.17, '0x318', 'byte2 wipers', 'steps to 0x0D, held ~11 s, then off', 'Wipers low, then off. 0x0D is the held low/continuous value.', 'confirmed by script'),
    (111.81, 117.75, '0x332', 'byte2', '0x18 -> 0x58 for 5.9 s, then back', 'Brake pedal held ~6 s, then released. Confirms the run-1 0x332 guess.', 'confirmed by script'),
    (128.45, 133.87, '0x318 / 0x3B2', 'left turn', '0x318 b0=01; 0x3B2 b0 blinks 0x0B', 'Left turn signal (script did left first).', 'confirmed by script'),
    (135.25, 140.37, '0x318 / 0x3B2', 'right turn', '0x318 b0=02; 0x3B2 b0 blinks 0x13', 'Right turn signal.', 'confirmed by script'),
    (142.21, 143.19, '0x122 / 0x322', 'crank / start', '0x122 05 then 15; RPM>200 at 143.06', 'Engine start.', 'confirmed by script; 0x122 crank codes educated guess vs Jeep map'),
    (143.19, 170.00, '0x322 / 0x2C2', 'idle', 'RPM ~700; battery charging', 'Idle after start (about 27 s vs the 30 s in the script).', 'confirmed by script'),
    (170.00, 180.00, '0x322', 'RPM', 'blip to ~2154', 'First no-load rev ~2000 rpm.', 'confirmed by script'),
    (185.00, 195.00, '0x322', 'RPM', 'blip to ~2131, then 0 at 199.95', 'Second no-load rev, then key off at 199.75 (0x122 -> 03 then 00 01).', 'confirmed by script'),
]

EVENTS_CANC = [
    (0.00, 16.29, 'bus rate', 'key off', 'Traffic winds down 0-10 s, then ~6 s of silence, 0x108 starts at 16.29', 'Key off, then key ON. CAN-C diagnostic traffic is quiet until key on.', 'confirmed by script'),
    (16.29, 29.53, '0x108 / 0x122', 'key on, RPM=0', '0x108 present, RPM 0', 'Key ON, engine off.', 'confirmed by script'),
    (29.53, 42.88, '0x334', 'byte0', '0x40 -> 00 at 29.53, back to 0x40 at 42.88', 'Lock (byte0=0x00), wait, unlock (byte0=0x40). Opposite polarity from IHS 0x2FA byte0. 0x1C0 unchanged.', 'confirmed by script'),
    (55.82, 68.24, '0x334', 'bytes 6 and 7', 'b6=C8, b7=02, b2=01 for ~12 s, then 0', 'Headlights on, then off. 0x334 byte6 is illumination output, not the wheel itself.', 'confirmed by script'),
    (68.24, 103.71, '0x334', 'byte6', 'stayed 00 the whole dimmer window; no other 0-200 proportional byte found', 'Dimmer step with headlights off: no proportional dimmer signal on CAN-C.', 'confirmed by script'),
    (103.71, 114.35, '0x318 / 0x334', 'wipers', '0x318 b2 held at 0x0D; 0x334 b7=01 while wiping', 'Wipers low, then off.', 'confirmed by script'),
    (126.56, 132.68, '0x11C / 0x140', 'brake', '0x11C pedal raw 0-1199; 0x140 byte0 80->85->80, ~6 s', 'Brake pedal held ~6 s, then released.', 'confirmed by script; known (opendbc)'),
    (143.15, 149.47, '0x318 / 0x334', 'left turn', '0x318 b0=01; 0x334 b3 blinks 0x40. No 0x02 / 0x80 in this session.', 'Left turn only. Right turn was not logged on this CAN-C pass.', 'confirmed by script (left); right missing in this file'),
    (154.69, 155.50, '0x108', 'ENGINE_RPM', 'last 0 at 154.69, first >200 at 155.12, peak crank then ~700 idle', 'Engine start.', 'confirmed by script; known (opendbc)'),
    (155.12, 189.45, '0x108', 'idle', 'RPM ~700', 'Idle after start (~34 s vs 30 s script).', 'confirmed by script'),
    (189.45, 195.59, '0x120 / 0x108 / 0x30A', 'rev 1', 'APS to 11.6%; RPM to ~2040; 0x30A b6 0->27 with lag', 'First no-load ~2000 rpm rev.', 'confirmed by script'),
    (202.65, 210.51, '0x120 / 0x108 / 0x30A', 'rev 2', 'APS to 11.2%; RPM to 2131; 0x30A b6 0->31 with lag', 'Second no-load ~2000 rpm rev (about 7 s after the first ended).', 'confirmed by script'),
    (217.53, 235.60, '0x108 / bus rate', 'key off', '0x108 stops at 217.53 still showing ~700; traffic then decays', 'Key off. ECM stopped broadcasting rather than sending RPM=0.', 'confirmed by script'),
]

def main():
    c = load(LOGS / 'pins_6-14_run2.csv')
    i = load(LOGS / 'pins_3-11_run2.csv')
    print('CAN-C', len(c), f'{c.t.max():.3f}s', c.can_id.nunique(), 'IDs', f'{len(c)/c.t.max():.1f} fps')
    print('IHS  ', len(i), f'{i.t.max():.3f}s', i.can_id.nunique(), 'IDs', f'{len(i)/i.t.max():.1f} fps')
    cols = ['log','t_start_s','t_end_s','id','signal','observed','interpretation','confidence']
    pd.DataFrame([{'log':'pins_3-11_run2.csv','t_start_s':a,'t_end_s':b,'id':c,'signal':d,'observed':e,'interpretation':f,'confidence':g} for a,b,c,d,e,f,g in EVENTS_IHS]).to_csv(HERE/'events_pins_3-11_run2.csv', index=False)
    pd.DataFrame([{'log':'pins_6-14_run2.csv','t_start_s':a,'t_end_s':b,'id':c,'signal':d,'observed':e,'interpretation':f,'confidence':g} for a,b,c,d,e,f,g in EVENTS_CANC]).to_csv(HERE/'events_pins_6-14_run2.csv', index=False)
    p1 = plot_dimmer(c, i)
    p2 = plot_locks_turns(c, i)
    p3 = plot_boost(c)
    print('plots:', p1.name, p2.name, p3.name)

if __name__ == '__main__':
    main()
