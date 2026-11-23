#!/usr/bin/env python3
"""v4 drive log (pins_6-14_v4drive.csv): per-event stats + 3 plots for Gauge 2 / Gauge 3.
Needs pid_poll_decode.py outputs (pid_poll/pins_6-14_v4drive_events.csv). Color-blind safe: blue/orange/gold,
line styles, labels; exhaust-brake periods hatched."""
from pathlib import Path
import numpy as np, pandas as pd
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
import pid_poll_decode as P

HERE = Path(__file__).resolve().parent
LOG = HERE.parent / 'data' / 'logs' / 'pins_6-14_v4drive.csv'
PLOTS = HERE / 'plots'
BLUE, ORANGE, GOLD, SKY = P.BLUE, P.ORANGE, P.GOLD, P.SKY
K = P.KPA_PSI

fr = P.read_frames(LOG); t0 = fr[0][0]
def bc(cid, f, n=3):
    a = np.array([((t - t0) / 1e6, f(d)) for t, c, e, d in fr if not e and c == cid and len(d) >= n]); return a[:, 0], a[:, 1]
BC = {'boost': bc(0x30A, lambda d: d[6] * K, 8), 'b7': bc(0x30A, lambda d: d[7], 8), 'load101': bc(0x101, lambda d: d[2]),
      'mph': bc(0x11C, lambda d: ((d[4] << 8) | d[5]) * 0.0078125 * 0.621371, 8), 'aps': bc(0x120, lambda d: d[2] * 0.4),
      'rpm': bc(0x108, lambda d: (d[0] << 8) | d[1]), 'th': bc(0x170, lambda d: (d[1] >> 1) & 1)}
ev = pd.read_csv(HERE / 'pid_poll' / 'pins_6-14_v4drive_events.csv')
def pid(s): g = ev[ev.signal == s]; return g.t_s.to_numpy(float), g.value.to_numpy(float)
def at(name, t): tt, vv = BC[name]; return np.interp(t, tt, vv)
def seg(t, m, gap, minlen):
    out, s, last = [], None, None
    for ti, mi in zip(t, m):
        if mi:
            if s is None: s = ti
            elif ti - last > gap: out.append((s, last)); s = ti
            last = ti
    if s is not None: out.append((s, last))
    return [x for x in out if x[1] - x[0] >= minlen]
pulls = seg(*BC['boost'][:1], BC['boost'][1] > 8, 1.5, 0.5)
brakes = seg(*BC['b7'][:1], BC['b7'][1] > 0, 1.0, 0.3)

def shade(ax):
    for a, b in brakes: ax.axvspan(a, b, facecolor=SKY, alpha=0.18, hatch='///', edgecolor=BLUE, lw=0)
def tags(ax):
    y = ax.get_ylim()[1]
    for i, (a, b) in enumerate(pulls, 1): ax.text((a + b) / 2, y, f'P{i}', ha='center', va='bottom', fontsize=8, weight='bold')
    for i, (a, b) in enumerate(brakes, 1): ax.text((a + b) / 2, y, f'B{i}', ha='center', va='bottom', fontsize=8, color=BLUE)

# ---------------- plot 1: overview
fig, ax = plt.subplots(4, 1, figsize=(15, 13), sharex=True)
t, v = BC['boost']; ax[0].plot(t, v, color=BLUE, lw=1.3, label='Boost, broadcast 0x30A b6 (psi)')
t, v = pid('map_psi_g'); ax[0].plot(t, v, color=ORANGE, ls='--', lw=1.3, label='MAP, PID 87 (psi gauge)')
t, v = pid('exh_press_psi_g_CHECK'); ax[0].plot(t, v, color=GOLD, ls=':', lw=2, label='Exhaust pressure, PID 73 (psi gauge)')
ax[0].set_ylabel('psi'); ax[0].set_title('Boost / MAP / exhaust back pressure')
t, v = pid('frp_cmd_psi'); ax[1].plot(t, v / 1000, color=BLUE, lw=1.3, label='FRP commanded (PID 6D)')
t, v = pid('frp_act_psi'); ax[1].plot(t, v / 1000, color=ORANGE, ls='--', lw=1.3, label='FRP actual (PID 6D)')
ax[1].set_ylabel('x1000 psi'); ax[1].set_title('Fuel rail pressure')
t, v = pid('turbo_rpm'); ax[2].plot(t, v / 1000, color=BLUE, lw=1.3, label='Turbo speed, PID 74 (x1000 rpm)')
t, v = BC['rpm']; ax[2].plot(t, v / 100, color=ORANGE, ls='--', lw=1.0, label='Engine rpm /100')
t, v = BC['b7']; ax[2].plot(t, v * 1.32, color=GOLD, ls=':', lw=2, label='Exhaust-brake HP estimate (0x30A b7 x1.32, guess)')
t, v = BC['mph']; ax[2].plot(t, v, color='black', ls='-.', lw=0.8, label='Speed mph')
ax[2].set_title('Turbo / rpm / exhaust-brake HP / speed')
t, v = pid('load_pct'); ax[3].plot(t, v, color=BLUE, lw=1.5, label='Load, PID 04 (%)')
t, v = BC['load101']; ax[3].plot(t, v / 2.55, color=ORANGE, ls='--', lw=1.0, label='Broadcast 0x101 b2 / 2.55 (%)')
t, v = pid('torque_act_pct'); ax[3].plot(t, v, color=GOLD, ls=':', lw=1.8, label='Actual torque, PID 62 (%)')
t, v = pid('fuel_rate_gph'); ax[3].plot(t, v, color='black', ls='-.', lw=0.8, label='Fuel rate, PID 5E (gal/h)')
ax[3].set_ylabel('% / gal/h'); ax[3].set_xlabel('time (s)'); ax[3].set_title('Load / torque / fuel rate')
for a in ax: shade(a); a.grid(alpha=0.35); a.legend(loc='upper left', fontsize=8, framealpha=0.9)
for a in ax: tags(a)
fig.suptitle('v4 drive (pins 6/14): P = boost pulls, B = exhaust-brake events (hatched ///). Exhaust brake switch = FULL all log; Tow/Haul on from 71 s', fontsize=10)
fig.tight_layout(); fig.savefig(PLOTS / 'v4drive_overview.png', dpi=100); plt.close(fig)

# ---------------- plot 2: exhaust brake
fig, ax = plt.subplots(1, 3, figsize=(16, 5), gridspec_kw={'width_ratios': [2.2, 1, 1]})
a0, a1 = brakes[0][0] - 8, brakes[-1][1] + 4
t, v = pid('exh_press_psi_g_CHECK'); m = (t > a0) & (t < a1); ax[0].plot(t[m], v[m], color=GOLD, lw=2, label='Exhaust pressure PID 73 (psi gauge)')
t, v = BC['b7']; m = (t > a0) & (t < a1); ax[0].plot(t[m], v[m] * 1.32, color=BLUE, ls='--', lw=1.3, label='Brake HP estimate b7 x1.32 (guess)')
t, v = BC['boost']; m = (t > a0) & (t < a1); ax[0].plot(t[m], v[m], color=ORANGE, ls=':', lw=1.5, label='Boost psi (0x30A b6)')
shade(ax[0]); ax[0].set_xlim(a0, a1); tags(ax[0]); ax[0].grid(alpha=.35); ax[0].legend(fontsize=8, loc='upper left'); ax[0].set_xlabel('time (s)')
ax[0].set_title('Exhaust brake events (hatched): back pressure vs HP estimate', pad=14)
te, ve = pid('exh_press_psi_g_CHECK'); b7 = at('b7', te); rpm = at('rpm', te); on = b7 > 3
ax[1].scatter(ve[on], b7[on] * 1.32, s=14, color=BLUE, marker='o', label='brake on')
ax[1].set_xlabel('Exhaust pressure PID 73 (psi gauge)'); ax[1].set_ylabel('HP estimate (b7 x1.32)'); ax[1].grid(alpha=.35)
ax[1].set_title(f'HP vs EBP  r={np.corrcoef(ve[on], b7[on])[0,1]:.2f}'); ax[1].legend(fontsize=8)
ax[2].scatter(rpm[on], b7[on] * 1.32, s=14, color=ORANGE, marker='^', label='brake on')
ax[2].set_xlabel('Engine rpm'); ax[2].set_ylabel('HP estimate (b7 x1.32)'); ax[2].grid(alpha=.35)
ax[2].set_title(f'HP vs rpm  r={np.corrcoef(rpm[on], b7[on])[0,1]:.2f}'); ax[2].legend(fontsize=8)
fig.tight_layout(); fig.savefig(PLOTS / 'v4drive_exhaust_brake.png', dpi=100); plt.close(fig)

# ---------------- plot 3: cross-checks
fig, ax = plt.subplots(1, 3, figsize=(16, 5.2))
tm, vm = pid('map_psi_g'); vb = at('boost', tm)
ax[0].scatter(vb, vm, s=10, color=BLUE, marker='o', label='samples'); ax[0].plot([0, 35], [0, 35], color=GOLD, ls='--', label='1:1')
ax[0].set_xlabel('Broadcast boost 0x30A b6 x0.145 (psi)'); ax[0].set_ylabel('PID 87 MAP, 1/32 kPa - 97 (psi)')
ax[0].set_title(f'MAP (87) vs broadcast boost: median diff {np.median(vm - vb):+.2f} psi'); ax[0].grid(alpha=.35); ax[0].legend(fontsize=8)
tl, vl = pid('load_pct'); t1, v1 = BC['load101']; b2 = v1[np.clip(np.searchsorted(t1, tl), 0, len(t1) - 1)]
ax[1].scatter(np.round(vl * 2.55), b2, s=10, color=ORANGE, marker='s', label='samples'); ax[1].plot([0, 215], [0, 215], color=BLUE, ls='--', label='1:1')
ax[1].set_xlabel('PID 04 raw A (load % x 2.55)'); ax[1].set_ylabel('Broadcast 0x101 b2'); ax[1].grid(alpha=.35); ax[1].legend(fontsize=8)
ax[1].set_title(f'Load: 0x101 b2 vs PID 04  r={np.corrcoef(np.round(vl*2.55), b2)[0,1]:.3f}')
tc, vc = pid('frp_cmd_psi'); _, va = pid('frp_act_psi')
ax[2].scatter(vc / 1000, va / 1000, s=10, color=BLUE, marker='o', label='samples'); ax[2].plot([4, 27], [4, 27], color=GOLD, ls='--', label='1:1')
ax[2].set_xlabel('FRP commanded (x1000 psi)'); ax[2].set_ylabel('FRP actual (x1000 psi)'); ax[2].grid(alpha=.35); ax[2].legend(fontsize=8)
ax[2].set_title('FRP actual vs commanded (off-line points = tip-in/out lag)')
fig.tight_layout(); fig.savefig(PLOTS / 'v4drive_crosschecks.png', dpi=100); plt.close(fig)
print('pulls', [(round(a, 1), round(b, 1)) for a, b in pulls]); print('brakes', [(round(a, 1), round(b, 1)) for a, b in brakes])
print('plots:', *(PLOTS / n for n in ('v4drive_overview.png', 'v4drive_exhaust_brake.png', 'v4drive_crosschecks.png')))
