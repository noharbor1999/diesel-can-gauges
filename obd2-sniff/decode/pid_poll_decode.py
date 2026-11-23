#!/usr/bin/env python3
"""Decode a SavvyCAN CSV captured with sniffer_node_C6_v4 (broadcast CAN-C + polled Mode 01 PIDs).

2017 Ram 3500 6.7 Cummins, CAN-C (OBD pins 6/14) 500k.

Input : SavvyCAN CSV  (Time Stamp,ID,Extended,Dir,Bus,LEN,D1..D8)  - same as data/logs/*.csv
Output (next to the decode/ folder unless --outdir):
  pid_poll/<stem>_events.csv   one row per decoded value (t_s, source, signal, value, unit, raw)
  pid_poll/<stem>_wide.csv     all signals on a 0.1 s grid, last value held (max 1 s), NaN otherwise
  pid_poll/<stem>_markers.csv  capture markers (0x1FFFFFF0)
  pid_poll/<stem>_poll_stats.csv  per-PID requests / replies / Hz / latency
  plots/<stem>_pid_poll.png    4 panels, blue/orange/gold + line styles + labels (color-blind safe)

Polled PIDs (ECM 0x7E0 -> 0x7E8, ISO-TP reassembled):
  6D  FRP cmd / actual psi  : B-C and D-E x10 kPa                       (verified on truck 11/22)
  73  exhaust pressure psi g: B-C x0.01 kPa abs - baro                   (CHECK: scale not yet proven)
  87  MAP psi gauge         : B-C x1/32 kPa abs - baro (SAE J1979 scale; x0.01 would give 37 kPa abs at
                              a rev, below atmosphere, so 1/32 is used - matches the probe's 117 kPa)
  74  turbo rpm             : B-C x10
  04  load %  A*100/255     62  actual torque %  A-125
  5E  fuel rate  (256A+B)/20 L/h -> gal/h      0C  rpm  (256A+B)/4
Broadcast CAN-C:
  0x30A b6 boost kPa gauge (psi = x0.145), b2 exhaust-brake switch (00 off / 80 full / A0 auto),
  b7 exhaust-brake effort counts, HP ~= b7 x 1.32 (educated-guess fit, +-3 hp)
Markers: extended 0x1FFFFFF0  AA <n> <millis LE x4> 55 55

Usage:  python3 pid_poll_decode.py LOG.csv [--baro 97] [--outdir DIR]
        python3 pid_poll_decode.py --selftest      (builds a synthetic log and checks the decodes)
"""
from __future__ import annotations
import argparse, csv, math, os, random, sys, tempfile
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

HERE = Path(__file__).resolve().parent
BLUE, ORANGE, GOLD, SKY = '#0072B2', '#E69F00', '#CC9A06', '#56B4E9'
KPA_PSI = 0.1450377
REQ_IDS = (0x7DF, 0x7E0)
FC_ID = 0x7E0
RESP_ID = 0x7E8
MARKER_ID = 0x1FFFFFF0
POLL_PIDS = [0x6D, 0x73, 0x87, 0x74, 0x04, 0x62, 0x5E, 0x0C]
BRAKE_SW = {0x00: 'off', 0x80: 'full', 0xA0: 'auto'}


# ------------------------------------------------------------------ reading
def read_frames(path: Path):
    """Yield (t_us, id, ext, data bytes) sorted by timestamp (stable)."""
    rows = []
    with open(path, newline='') as fh:
        r = csv.reader(fh)
        hdr = next(r)
        col = {h.strip(): i for i, h in enumerate(hdr)}
        iT, iID, iE, iL = col['Time Stamp'], col['ID'], col['Extended'], col['LEN']
        iD = [col[f'D{k}'] for k in range(1, 9)]
        for n, row in enumerate(r):
            if len(row) <= iL or not row[iT].strip():
                continue
            try:
                t = int(row[iT]); cid = int(row[iID], 16); ln = int(row[iL])
            except ValueError:
                continue
            ext = row[iE].strip().lower() == 'true'
            d = []
            for k in range(min(ln, 8)):
                v = row[iD[k]].strip() if iD[k] < len(row) else ''
                d.append(int(v, 16) if v else 0)
            rows.append((t, n, cid, ext, bytes(d)))
    rows.sort(key=lambda x: (x[0], x[1]))
    return [(t, cid, ext, d) for t, _, cid, ext, d in rows]


# ------------------------------------------------------------------ decoding
def decode_pid(pid: int, p: bytes, baro: float):
    """p = payload after '41 pid'. Returns list of (signal, value, unit)."""
    out = []
    u16 = lambda i: (p[i] << 8) | p[i + 1]
    try:
        if pid == 0x6D and len(p) >= 5:
            out += [('frp_cmd_psi', u16(1) * 10 * KPA_PSI, 'psi'), ('frp_act_psi', u16(3) * 10 * KPA_PSI, 'psi')]
        elif pid == 0x73 and len(p) >= 3:
            out.append(('exh_press_psi_g_CHECK', (u16(1) * 0.01 - baro) * KPA_PSI, 'psi gauge'))
        elif pid == 0x87 and len(p) >= 3:
            out.append(('map_psi_g', (u16(1) / 32.0 - baro) * KPA_PSI, 'psi gauge'))
        elif pid == 0x74 and len(p) >= 3:
            out.append(('turbo_rpm', u16(1) * 10, 'rpm'))
        elif pid == 0x04 and len(p) >= 1:
            out.append(('load_pct', p[0] * 100 / 255, '%'))
        elif pid == 0x62 and len(p) >= 1:
            out.append(('torque_act_pct', p[0] - 125, '%'))
        elif pid == 0x5E and len(p) >= 2:
            out.append(('fuel_rate_gph', u16(0) / 20 / 3.78541, 'gal/h'))
        elif pid == 0x0C and len(p) >= 2:
            out.append(('rpm_pid', u16(0) / 4, 'rpm'))
    except IndexError:
        pass
    return out


def decode(frames, baro: float):
    events, markers = [], []
    t0 = frames[0][0] if frames else 0
    iso = None                      # [t_first, expected, nextSn, bytearray]
    pending = {}                    # pid -> request t_us (latest)
    stats = defaultdict(lambda: {'req': 0, 'reply': 0, 'neg': 0, 'lat_ms': []})
    nfc = 0
    last_sw = None

    def finish(t, payload: bytes):
        if len(payload) >= 1 and payload[0] == 0x7F and len(payload) >= 3:
            stats[payload[2]]['neg'] += 1
            return
        if len(payload) < 2 or payload[0] != 0x41:
            return
        pid = payload[1]
        st = stats[pid]; st['reply'] += 1
        if pid in pending:
            st['lat_ms'].append((t - pending.pop(pid)) / 1000)
        for sig, val, unit in decode_pid(pid, payload[2:], baro):
            events.append(((t - t0) / 1e6, f'pid{pid:02X}', sig, val, unit, payload.hex(' ').upper()))

    for t, cid, ext, d in frames:
        ts = (t - t0) / 1e6
        if ext:
            if cid == MARKER_ID and len(d) >= 8 and d[0] == 0xAA:
                ms = d[2] | d[3] << 8 | d[4] << 16 | d[5] << 24
                markers.append((ts, d[1], ms))
            continue
        if cid in REQ_IDS and len(d) >= 3:
            if d[0] == 0x02 and d[1] == 0x01:
                stats[d[2]]['req'] += 1
                pending[d[2]] = t
            elif d[0] >> 4 == 0x3:
                nfc += 1
            continue
        if cid == RESP_ID and d:
            pci = d[0] >> 4
            if pci == 0:
                n = d[0] & 0x0F
                if 0 < n <= 7:
                    finish(t, bytes(d[1:1 + n]))
            elif pci == 1:
                exp = ((d[0] & 0x0F) << 8) | d[1]
                iso = [t, exp, 1, bytearray(d[2:8])]
            elif pci == 2 and iso:
                if (d[0] & 0x0F) != iso[2]:
                    iso = None; continue
                iso[2] = (iso[2] + 1) & 0x0F
                iso[3] += d[1:8]
                if len(iso[3]) >= iso[1]:
                    finish(t, bytes(iso[3][:iso[1]])); iso = None
            continue
        if cid == 0x30A and len(d) >= 8:
            events.append((ts, 'bc30A', 'boost_bc_psi', d[6] * KPA_PSI, 'psi gauge', d.hex(' ').upper()))
            events.append((ts, 'bc30A', 'ebrake_effort_cnt', d[7], 'counts', ''))
            events.append((ts, 'bc30A', 'ebrake_hp_GUESS', d[7] * 1.32, 'hp', ''))
            if d[2] != last_sw:
                events.append((ts, 'bc30A', 'ebrake_switch', d[2], BRAKE_SW.get(d[2], f'0x{d[2]:02X}'), ''))
                last_sw = d[2]
    dur = (frames[-1][0] - t0) / 1e6 if frames else 0
    return events, markers, stats, nfc, dur


# ------------------------------------------------------------------ outputs
def write_outputs(path: Path, events, markers, stats, nfc, dur, outdir: Path, plotdir: Path):
    outdir.mkdir(parents=True, exist_ok=True); plotdir.mkdir(parents=True, exist_ok=True)
    stem = path.stem
    ev = pd.DataFrame(events, columns=['t_s', 'source', 'signal', 'value', 'unit', 'raw'])
    ev.to_csv(outdir / f'{stem}_events.csv', index=False, float_format='%.4f')
    pd.DataFrame(markers, columns=['t_s', 'marker', 'node_millis']).to_csv(outdir / f'{stem}_markers.csv', index=False)

    grid = np.round(np.arange(0, dur + 0.1, 0.1), 3)
    wide = pd.DataFrame({'t_s': grid})
    for sig, g in ev.groupby('signal', sort=False):
        g = g.sort_values('t_s')
        tv, vv = g.t_s.to_numpy(float), g.value.to_numpy(float)
        idx = np.searchsorted(tv, grid, side='right') - 1
        col = np.where(idx >= 0, vv[np.clip(idx, 0, None)], np.nan)
        age = np.where(idx >= 0, grid - tv[np.clip(idx, 0, None)], np.inf)
        hold = 3600 if sig == 'ebrake_switch' else 1.0
        wide[sig] = np.where(age <= hold, col, np.nan)
    wide.to_csv(outdir / f'{stem}_wide.csv', index=False, float_format='%.3f')

    srows = []
    for pid in POLL_PIDS + sorted(k for k in stats if k not in POLL_PIDS):
        s = stats.get(pid)
        if not s or (s['req'] == 0 and s['reply'] == 0):
            continue
        lat = s['lat_ms']
        srows.append({'pid': f'{pid:02X}', 'requests': s['req'], 'replies': s['reply'], 'negative': s['neg'],
                      'missed': max(0, s['req'] - s['reply'] - s['neg']),
                      'reply_hz': round(s['reply'] / dur, 2) if dur else 0,
                      'lat_ms_median': round(float(np.median(lat)), 1) if lat else None,
                      'lat_ms_max': round(float(np.max(lat)), 1) if lat else None})
    st = pd.DataFrame(srows)
    st.to_csv(outdir / f'{stem}_poll_stats.csv', index=False)

    # ---- plot
    def series(sig):
        g = ev[ev.signal == sig]
        return g.t_s.to_numpy(float), g.value.to_numpy(float)
    fig, ax = plt.subplots(4, 1, figsize=(13, 12), sharex=True)
    for sig, c, ls, lab in [('frp_cmd_psi', BLUE, '-', 'FRP commanded (6D)'), ('frp_act_psi', ORANGE, '--', 'FRP actual (6D)')]:
        t, v = series(sig); ax[0].plot(t, v, color=c, ls=ls, lw=1.4, label=lab)
    ax[0].set_ylabel('psi'); ax[0].set_title('Fuel rail pressure')
    for sig, c, ls, lab in [('boost_bc_psi', BLUE, '-', 'Boost broadcast 0x30A b6'), ('map_psi_g', ORANGE, '--', 'MAP gauge (PID 87)'),
                            ('exh_press_psi_g_CHECK', GOLD, ':', 'Exhaust pressure gauge (PID 73, CHECK)')]:
        t, v = series(sig); ax[1].plot(t, v, color=c, ls=ls, lw=1.6, label=lab)
    ax[1].set_ylabel('psi gauge'); ax[1].set_title('Boost / MAP / exhaust pressure')
    t, v = series('turbo_rpm'); ax[2].plot(t, v / 1000, color=BLUE, lw=1.4, label='Turbo speed (74) x1000 rpm')
    t, v = series('rpm_pid'); ax[2].plot(t, v / 100, color=ORANGE, ls='--', lw=1.4, label='Engine rpm (0C) x100')
    t, v = series('ebrake_hp_GUESS'); ax[2].plot(t, v / 10, color=GOLD, ls=':', lw=1.6, label='Exh-brake HP guess /10 (0x30A b7)')
    ax[2].set_ylabel('scaled'); ax[2].set_title('Turbo / rpm / exhaust-brake effort')
    t, v = series('load_pct'); ax[3].plot(t, v, color=BLUE, lw=1.4, label='Load % (04)')
    t, v = series('torque_act_pct'); ax[3].plot(t, v, color=ORANGE, ls='--', lw=1.4, label='Actual torque % (62)')
    t, v = series('fuel_rate_gph'); ax[3].plot(t, v * 5, color=GOLD, ls=':', lw=1.6, label='Fuel rate gal/h x5 (5E)')
    ax[3].set_ylabel('% / scaled'); ax[3].set_xlabel('time (s)'); ax[3].set_title('Load / torque / fuel rate')
    # exhaust-brake switch shading (hatched, not color-only)
    sw = ev[ev.signal == 'ebrake_switch'].sort_values('t_s')
    sw_t, sw_v = sw.t_s.to_list() + [dur], sw.value.to_list()
    for i, val in enumerate(sw_v):
        if val in (0x80, 0xA0):
            for a in ax:
                a.axvspan(sw_t[i], sw_t[i + 1], color=SKY, alpha=0.12, hatch='//' if val == 0x80 else '..', lw=0)
    for a in ax:
        for mt, mn, _ in markers:
            a.axvline(mt, color='black', ls='-.', lw=0.8)
        a.grid(True, alpha=0.35)
        a.legend(loc='upper left', fontsize=8, framealpha=0.9)
    for mt, mn, _ in markers:
        ax[0].text(mt, ax[0].get_ylim()[1], f' M{mn}', va='top', fontsize=8)
    fig.suptitle(f'{path.name}: polled PIDs + broadcast (shaded = exhaust brake ON, // full, .. auto; dash-dot = markers)', fontsize=10)
    fig.tight_layout()
    png = plotdir / f'{stem}_pid_poll.png'
    fig.savefig(png, dpi=110); plt.close(fig)
    return st, png


# ------------------------------------------------------------------ self test
def make_synthetic(path: Path, seconds=20.0, seed=1):
    """Synthetic v4-style log: 0x30A broadcast at 50 Hz, filler IDs, polled PIDs with ISO-TP 6D,
    echoed requests + flow control, a few unanswered requests, 2 markers, some rows out of order."""
    rnd = random.Random(seed)
    rows = []
    def add(t_us, cid, data, ext=False):
        rows.append([t_us, cid, ext, list(data)])
    T0 = 77_000_000
    t = 0.0
    while t < seconds:                                      # broadcast
        us = T0 + int(t * 1e6)
        boost_kpa = int(max(0, 200 * math.sin(math.pi * t / 10))) if t < 10 else 0
        sw = 0x80 if 12 <= t < 18 else 0x00
        eff = int(150 * math.sin(math.pi * (t - 12) / 6)) if 12 <= t < 18 else 0
        add(us, 0x30A, [0, 0, sw, 0, 0, 0, boost_kpa, max(0, eff)])
        add(us + 300, 0x108, [0x0A, 0xDD, 0, 0, 0, 0, 0, rnd.randrange(256)])
        t += 0.02
    truth = {}
    t, k, n_req = 3.0, 0, 0
    while t < seconds:
        pid = POLL_PIDS[k % 8]; k += 1
        us = T0 + int(t * 1e6)
        add(us, 0x7E0, [0x02, 0x01, pid, 0, 0, 0, 0, 0]); n_req += 1
        r = us + 8000
        if 6.0 <= t < 6.3:                                  # simulated dropouts: no reply
            t += 0.031; continue
        boost = max(0.0, 200 * math.sin(math.pi * t / 10)) if t < 10 else 0.0
        if pid == 0x6D:
            cmd, act = 3277 + int(boost * 20), 3240 + int(boost * 20)   # x10 kPa
            pay = [0x41, 0x6D, 0x03, cmd >> 8, cmd & 255, act >> 8, act & 255, 0, 0, 0, 0, 0, 0]
            add(r, 0x7E8, [0x10, 13] + pay[:6])
            add(r + 1500, 0x7E0, [0x30, 0, 0, 0, 0, 0, 0, 0])
            add(r + 2500, 0x7E8, [0x21] + pay[6:13])
            truth.setdefault('frp', []).append((cmd * 10 * KPA_PSI, act * 10 * KPA_PSI))
        else:
            if pid == 0x73:
                v = int((97 + boost * 0.6 + (40 if 12 <= t < 18 else 0)) * 100); data = [0x01, v >> 8, v & 255, 0, 0]
            elif pid == 0x87:
                v = int((97 + boost) * 32); data = [0x01, v >> 8, v & 255, 0, 0]
            elif pid == 0x74:
                v = 1480 + int(boost * 15); data = [0x01, v >> 8, v & 255, 0, 0]
            elif pid == 0x04:
                data = [int(12 + boost)]
            elif pid == 0x62:
                data = [125 + int(boost / 3)]
            elif pid == 0x5E:
                v = 107 + int(boost * 3); data = [v >> 8, v & 255]
            else:
                v = 2780 + int(boost * 40); data = [v >> 8, v & 255]
            pay = [0x41, pid] + data
            add(r, 0x7E8, [len(pay)] + pay + [0] * (7 - len(pay)))
        t += 0.031
    for i, mt in enumerate((5.0, 13.0), 1):
        ms = int(mt * 1000) + 70000
        add(T0 + int(mt * 1e6), MARKER_ID, [0xAA, i, ms & 255, ms >> 8 & 255, ms >> 16 & 255, ms >> 24, 0x55, 0x55], ext=True)
    rows.sort(key=lambda x: x[0])
    rows[100:110] = rows[100:110][::-1]                     # a little disorder, like real SavvyCAN saves
    with open(path, 'w') as fh:
        fh.write('Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8\n')
        for tu, cid, ext, d in rows:
            fh.write(f"{tu},{cid:08X},{'true' if ext else 'false'},Rx,0,{len(d)}," + ','.join(f'{b:02X}' for b in d) + ',\n')
    return n_req, truth


def selftest():
    tmp = Path(tempfile.mkdtemp(prefix='pidpoll_selftest_'))
    log = tmp / 'synthetic_v4.csv'
    n_req, truth = make_synthetic(log)
    frames = read_frames(log)
    events, markers, stats, nfc, dur = decode(frames, 97.0)
    st, png = write_outputs(log, events, markers, stats, nfc, dur, tmp / 'pid_poll', tmp / 'plots')
    ev = pd.DataFrame(events, columns=['t_s', 'source', 'signal', 'value', 'unit', 'raw'])
    ok = True
    def check(name, cond):
        nonlocal ok
        print(f"  [{'PASS' if cond else 'FAIL'}] {name}"); ok &= bool(cond)
    frp = ev[ev.signal == 'frp_act_psi'].value.to_numpy()
    check(f"6D ISO-TP replies reassembled ({len(frp)} of {len(truth['frp'])})", len(frp) == len(truth['frp']))
    check('6D values match truth', np.allclose(frp[:len(truth['frp'])], [a for _, a in truth['frp']][:len(frp)]))
    check(f'idle FRP {frp[-1]:.0f} psi (~4700)', 4600 < frp[-1] < 4800)
    m = ev[ev.signal == 'map_psi_g'].value
    check(f'MAP gauge range {m.min():.1f}..{m.max():.1f} psi (expect ~0..29)', -0.2 < m.min() < 0.5 and 27 < m.max() < 30)
    e = ev[ev.signal == 'exh_press_psi_g_CHECK'].value
    check(f'exhaust pressure gauge max {e.max():.1f} psi', 15 < e.max() < 25)
    check('turbo rpm 14800 at idle', abs(ev[ev.signal == 'turbo_rpm'].value.min() - 14800) < 1)
    check('rpm 695 at idle', abs(ev[ev.signal == 'rpm_pid'].value.min() - 695) < 1)
    check('markers 2', len(markers) == 2 and markers[0][1] == 1)
    sw = ev[ev.signal == 'ebrake_switch']
    check('exhaust brake switch off->full->off', list(sw.value) == [0, 0x80, 0])
    check('flow-control frames counted', nfc > 0)
    s6d = st[st.pid == '6D'].iloc[0]
    check(f"poll stats: 6D req {s6d.requests} reply {s6d.replies} missed {s6d.missed}, ~{s6d.reply_hz} Hz", s6d.missed >= 1 and 3.5 < s6d.reply_hz * dur / (dur - 3) < 4.3)
    check(f'plot written {png}', png.exists() and png.stat().st_size > 20000)
    print(st.to_string(index=False))
    print(('SELFTEST OK' if ok else 'SELFTEST FAILED') + f'  ({tmp})')
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('csv', nargs='?')
    ap.add_argument('--baro', type=float, default=97.0, help='barometric kPa for gauge pressures (PID 33 read 97)')
    ap.add_argument('--outdir', type=Path, default=HERE / 'pid_poll')
    ap.add_argument('--plotdir', type=Path, default=HERE / 'plots')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.csv:
        ap.error('give a SavvyCAN CSV or --selftest')
    path = Path(a.csv)
    frames = read_frames(path)
    if not frames:
        print('no frames'); return 1
    events, markers, stats, nfc, dur = decode(frames, a.baro)
    st, png = write_outputs(path, events, markers, stats, nfc, dur, a.outdir, a.plotdir)
    print(f'{path.name}: {len(frames)} frames, {dur:.1f} s, {len(events)} decoded values, {len(markers)} markers, {nfc} flow-control frames')
    print(st.to_string(index=False) if len(st) else 'no polled PIDs found (v3 log or poller off?)')
    print(f'outputs: {a.outdir}  plot: {png}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
