#!/usr/bin/env python3
"""Dimmer-wheel sweep, lights ON — both buses, separate sessions."""
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

def load(path: Path) -> pd.DataFrame:
    df = pd.read_csv(path, index_col=False)
    df = df.loc[:, ~df.columns.astype(str).str.startswith('Unnamed')]
    df['can_id'] = [int(str(x), 16) for x in df['ID']]
    for i, col in enumerate(['D1','D2','D3','D4','D5','D6','D7','D8']):
        df[f'b{i}'] = [int(str(x), 16) if pd.notna(x) and str(x) not in ('','nan') else 0 for x in df[col]]
    df = df.sort_values('Time Stamp', kind='mergesort').reset_index(drop=True)
    ts = df['Time Stamp'].astype('int64')
    df['t'] = (ts - int(ts.iloc[0])) / 1e6
    return df

def style(ax, ylabel, title=None):
    ax.set_ylabel(ylabel)
    ax.grid(True, alpha=0.35)
    if title:
        ax.set_title(title)
    ax.legend(loc='upper right', framealpha=0.9, fontsize=8)

def main():
    i = load(LOGS / 'pins_3-11_dimmer.csv')
    c = load(LOGS / 'pins_6-14_dimmer.csv')
    print('IHS', len(i), f'{i.t.max():.3f}s', i.can_id.nunique())
    print('CAN-C', len(c), f'{c.t.max():.3f}s', c.can_id.nunique())

    events = [
        # IHS
        ( 'pins_3-11_dimmer.csv', 8.15, 38.01, '0x3B2 / 0x2FA', 'headlights',
          '0x3B2 b0 0x03->0x83 (131) at 8.15, back 0x03 at 38.01; 0x2FA b2 jumps 0->200 at 8.16 and 200->0 at 38.01',
          'Headlights on, then off. 0x2FA byte2 is 0 whenever lights are off.',
          'confirmed by script'),
        ( 'pins_3-11_dimmer.csv', 13.25, 14.67, '0x2FA', 'byte 6 wheel + byte 2 output',
          'b6 200->0 (1-count steps); b2 200->34 (floors at 34)',
          'First down-sweep (wheel started at max).',
          'confirmed by script'),
        ( 'pins_3-11_dimmer.csv', 18.50, 20.50, '0x2FA', 'byte 6 wheel + byte 2 output',
          'b6 0->200; b2 34->200',
          'Up-sweep min to max.',
          'confirmed by script'),
        ( 'pins_3-11_dimmer.csv', 26.50, 28.50, '0x2FA', 'byte 6 wheel + byte 2 output',
          'b6 200->0; b2 200->34',
          'Second down-sweep.',
          'confirmed by script'),
        ( 'pins_3-11_dimmer.csv', 30.50, 32.00, '0x2FA', 'byte 6 wheel + byte 2 output',
          'b6 0->200; b2 34->200',
          'Second up-sweep, then a partial down/up around 34.5-37 before lights off.',
          'confirmed by script'),
        # CAN-C (separate session)
        ( 'pins_6-14_dimmer.csv', 10.69, 34.73, '0x334', 'headlights + byte 6 output',
          'byte7 0->2 at 10.69, back 0 at 34.73; byte6 jumps 0->200 at lights on, 200->0 at lights off',
          'Headlights on, then off. 0x334 byte6 is 0 whenever lights are off (illumination output).',
          'confirmed by script'),
        ( 'pins_6-14_dimmer.csv', 14.59, 16.00, '0x334', 'byte 6 illumination output',
          '200->34, 1-count steps, floors at 34',
          'First down-sweep on CAN-C (separate clock from IHS).',
          'confirmed by script'),
        ( 'pins_6-14_dimmer.csv', 19.00, 20.50, '0x334', 'byte 6 illumination output',
          '34->200',
          'Up-sweep.',
          'confirmed by script'),
        ( 'pins_6-14_dimmer.csv', 27.50, 29.00, '0x334', 'byte 6 illumination output',
          '200->34',
          'Second down-sweep.',
          'confirmed by script'),
        ( 'pins_6-14_dimmer.csv', 31.00, 31.91, '0x334', 'byte 6 illumination output',
          '34->200, then holds until lights off at 34.73',
          'Second up-sweep.',
          'confirmed by script'),
    ]
    pd.DataFrame([{
        'log': a, 't_start_s': b, 't_end_s': c, 'id': d, 'signal': e,
        'observed': f, 'interpretation': g, 'confidence': h,
    } for a,b,c,d,e,f,g,h in events]).to_csv(HERE / 'events_dimmer.csv', index=False)

    fig, ax = plt.subplots(2, 1, figsize=(13.0, 8.6))
    r = i[i.can_id == 0x2FA]
    lit = i[i.can_id == 0x3B2]
    ax[0].plot(r.t, arr(r.b6), color=ORANGE, lw=1.6, label='0x2FA byte6  dimmer WHEEL 0-200 (confirmed by script)')
    ax[0].plot(r.t, arr(r.b2), color=BLUE, lw=1.3, label='0x2FA byte2  illumination OUTPUT (0=lights off; 34-200 when on)')
    ax[0].plot(r.t, arr(r.b4), color=GOLD, lw=1.0, ls='--', alpha=0.85, label='0x2FA byte4  0/200 companion (0 near wheel-min)')
    ax[0].step(lit.t, arr(lit.b0), where='post', color=SKY, lw=1.0, label='0x3B2 byte0  lighting flags (0x83=131 lights on)')
    ax[0].axvspan(8.15, 38.01, color=SKY, alpha=0.10)
    ax[0].set_xlim(0, 49)
    ax[0].set_xlabel('Seconds from start of pins_3-11_dimmer.csv (CAN-IHS)')
    style(ax[0], 'Raw byte', 'CAN-IHS — lights on, then several min↔max dimmer sweeps')

    r = c[c.can_id == 0x334]
    ax[1].plot(r.t, arr(r.b6), color=BLUE, lw=1.6, label='0x334 byte6  illumination OUTPUT (0=lights off; 34-200 when on)')
    ax[1].step(r.t, arr(r.b7) * 40, where='post', color=SKY, lw=1.1, label='0x334 byte7 × 40  (2=lights on, shown scaled)')
    ax[1].axvspan(10.69, 34.73, color=SKY, alpha=0.10)
    ax[1].set_xlim(0, 43)
    ax[1].set_xlabel('Seconds from start of pins_6-14_dimmer.csv (CAN-C)  —  separate session, clocks are not shared')
    style(ax[1], 'Raw byte', 'CAN-C — same sweep pattern on the BCM illumination-output byte')

    fig.suptitle('Dimmer sweep — proportional 0-200 level (wheel on IHS 0x2FA b6; output on 0x2FA b2 / 0x334 b6)', fontsize=12)
    fig.tight_layout()
    path = PLOTS / 'dimmer_sweep.png'
    fig.savefig(path, dpi=120)
    plt.close(fig)
    print('plot', path)

if __name__ == '__main__':
    main()
