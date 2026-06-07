#!/usr/bin/env python3
"""
EMG Studio : DTW monitor (window 3, v1).

Loads the templates from calibrate.py and shows, live, the DTW distance from the current
envelope to EACH template (relaxed / open / close). Lower distance = better match. This is
the reliability check: do relaxed, weak (open), and strong (close) separate cleanly?

Per template, the live window is the LAST <template-length> samples of the envelope,
resampled to a fixed length so DTW compares shapes at matched duration and amplitude
(amplitude is kept un-normalized, so a strong pulse and a weak pulse differ by magnitude).

Everything is logged to dtw_log_<timestamp>.csv for review. No gripper decision yet, that
goes in on top once the separation looks good.

Usage:  python operate.py --port COM6 [--mains 60]
Close the PlatformIO Serial Monitor first (one program per COM port).
"""
import os, sys, json, signal, argparse, csv, time
os.environ.setdefault('PYQTGRAPH_QT_LIB', 'PyQt6')
import numpy as np
import serial
from serial.tools import list_ports
from PyQt6 import QtWidgets, QtCore
import pyqtgraph as pg
from emg_studio import Ring, SerialReader

TEMPLATES_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'templates.json')
L = 40                      # resample length for DTW
COLORS = ['#888888', '#2980b9', '#e74c3c', '#27ae60', '#8e44ad', '#d35400']


def resample(a, n):
    a = np.asarray(a, dtype=float)
    if len(a) < 2:
        return np.zeros(n)
    return np.interp(np.linspace(0, len(a) - 1, n), np.arange(len(a)), a)


def dtw(a, b):
    """Classic DTW with an absolute-difference local cost (a,b same length L here)."""
    n, m = len(a), len(b)
    D = np.full((n + 1, m + 1), np.inf)
    D[0, 0] = 0.0
    for i in range(1, n + 1):
        ai = a[i - 1]
        Di, Dim = D[i], D[i - 1]
        for j in range(1, m + 1):
            c = abs(ai - b[j - 1])
            Di[j] = c + min(Dim[j], Di[j - 1], Dim[j - 1])
    return float(D[n, m])


class Monitor(QtWidgets.QMainWindow):
    def __init__(self, args, env_ring, raw_ring, groups, order):
        super().__init__()
        self.args = args
        self.env_ring = env_ring
        self.raw_ring = raw_ring
        self.groups = groups        # label -> list of {'n':, 'res':} (active templates)
        self.order = order          # label display order
        self.tick = 0
        self.t0 = time.time()

        # rolling history of each label's (min) DTW value, for the traces
        self.hist_n = int(args.fs_disp * args.seconds)
        self.dtw_hist = {label: Ring(self.hist_n, fill=np.nan) for label in order}
        self.tx = np.arange(self.hist_n) / args.fs_disp

        # CSV log
        stamp = time.strftime('%y%m%d-%H%M%S')
        self.logf = open(os.path.join(os.path.dirname(TEMPLATES_PATH), f'dtw_log_{stamp}.csv'),
                         'w', newline='')
        self.csv = csv.writer(self.logf)
        self.csv.writerow(['t_s'] + [f"dtw_{label}" for label in order]
                          + ['env', 'fsm', 'gripper', 'fired'])

        self.setWindowTitle('EMG Studio : DTW monitor')
        central = QtWidgets.QWidget(); self.setCentralWidget(central)
        root = QtWidgets.QVBoxLayout(central)

        self.health = QtWidgets.QLabel('connecting...')
        self.health.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        f = self.health.font(); f.setPointSize(12); f.setBold(True); self.health.setFont(f)
        self.health.setFixedHeight(38); self.health.setStyleSheet('background:#555;color:white;')
        root.addWidget(self.health)

        self.values = QtWidgets.QLabel('')
        self.values.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        vf = self.values.font(); vf.setPointSize(11); self.values.setFont(vf)
        root.addWidget(self.values)

        self.gripper_lbl = QtWidgets.QLabel('GRIPPER: OPEN   [IDLE]')
        self.gripper_lbl.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        gf = self.gripper_lbl.font(); gf.setPointSize(16); gf.setBold(True); self.gripper_lbl.setFont(gf)
        self.gripper_lbl.setFixedHeight(48); self.gripper_lbl.setStyleSheet('background:#2980b9;color:white;')
        root.addWidget(self.gripper_lbl)

        # live tunables
        ctrl = QtWidgets.QHBoxLayout()
        def mkspin(lo, hi, val, step):
            s = QtWidgets.QDoubleSpinBox(); s.setRange(lo, hi); s.setValue(val); s.setSingleStep(step)
            s.setMaximumWidth(90); return s
        self.sb_acton = mkspin(0, 600, 40, 5)
        self.sb_cap = mkspin(0, 9999, 900, 50)
        self.sb_margin = mkspin(1.0, 5.0, 1.5, 0.1)
        self.sb_lockout = mkspin(0, 5, 1.2, 0.1)
        for lbl, w in [('activity env >', self.sb_acton), ('DTW cap <', self.sb_cap),
                       ('margin x', self.sb_margin), ('lockout s', self.sb_lockout)]:
            ctrl.addWidget(QtWidgets.QLabel(lbl)); ctrl.addWidget(w)
        ctrl.addStretch(1)
        root.addLayout(ctrl)

        # event-based decision FSM
        self.state = 'IDLE'; self.gripper = 'open'; self.fired = ''
        self.min_act = {}; self.reset_timer = 0; self.lock_ticks = 0
        self.reset_need = max(1, int(0.3 * args.fs_disp))   # ~0.3 s of rest to re-arm

        pg.setConfigOption('background', 'w'); pg.setConfigOption('foreground', 'k')
        pg.setConfigOptions(antialias=True)
        glw = pg.GraphicsLayoutWidget(); root.addWidget(glw)

        self.p_env = glw.addPlot(row=0, col=0)
        self.p_env.setTitle('envelope'); self.p_env.setYRange(0, args.env_max)
        self.p_env.setXRange(0, args.seconds); self.p_env.showGrid(x=False, y=True, alpha=0.25)
        self.c_env = self.p_env.plot(pen=pg.mkPen('#e74c3c', width=2))
        self.tx_env = np.arange(env_ring.n) / args.fs

        self.p_dtw = glw.addPlot(row=1, col=0)
        self.p_dtw.setTitle('DTW distance per template  (lower = better match)')
        self.p_dtw.setXRange(0, args.seconds); self.p_dtw.showGrid(x=False, y=True, alpha=0.25)
        self.p_dtw.setLabel('bottom', 'seconds'); self.p_dtw.addLegend(offset=(10, 10))
        self.p_dtw.setXLink(self.p_env)
        self.curves = {}
        for i, label in enumerate(order):
            self.curves[label] = self.p_dtw.plot(
                pen=pg.mkPen(COLORS[i % len(COLORS)], width=2), name=label)

        self.timer = QtCore.QTimer(); self.timer.timeout.connect(self.update_view)
        self.timer.start(int(1000 / args.fs_disp))

    def step_fsm(self, env, vd):
        """Event-based pulse decision (see the README / discussion)."""
        self.fired = ''
        act_on = self.sb_acton.value(); act_off = act_on * 0.6
        cap = self.sb_cap.value(); margin = self.sb_margin.value()
        actions = {k: v for k, v in vd.items() if k != 'relaxed'}

        if self.state == 'IDLE':
            if env > act_on:                       # activity gate: a pulse begins
                self.state = 'PULSE'
                self.min_act = {k: float('inf') for k in actions}
        elif self.state == 'PULSE':
            for k in actions:                      # track best match reached during the pulse
                self.min_act[k] = min(self.min_act[k], actions[k])
            if env < act_off:                      # pulse ended -> decide
                best = min(self.min_act, key=self.min_act.get)
                bv = self.min_act[best]
                others = [v for k, v in self.min_act.items() if k != best]
                second = min(others) if others else float('inf')
                if bv < cap and second > bv * margin:   # clear + unambiguous
                    self.gripper = best
                    self.fired = best
                self.state = 'REFRACTORY'; self.reset_timer = 0; self.lock_ticks = 0
        elif self.state == 'REFRACTORY':           # re-arm once the muscle is back to rest
            self.lock_ticks += 1
            locked = self.lock_ticks < int(self.sb_lockout.value() * self.args.fs_disp)
            if not locked and env < act_off:        # envelope-return = the real relaxation (fast)
                self.reset_timer += 1
                if self.reset_timer >= self.reset_need:
                    self.state = 'IDLE'
            else:
                self.reset_timer = 0

    def update_view(self):
        if self.logf.closed:
            return
        snap = self.env_ring.snapshot()
        self.c_env.setData(self.tx_env, snap)
        env_now = float(np.mean(snap[-5:]))

        vd = {}
        for label in self.order:                 # DTW per label = best (min) over its active templates
            best = float('inf')
            for tpl in self.groups[label]:
                nk = min(tpl['n'], len(snap))
                d = dtw(resample(snap[-nk:], L), tpl['res'])
                if d < best:
                    best = d
            self.dtw_hist[label].push(best)
            vd[label] = best
        vals = [vd[label] for label in self.order]
        self.step_fsm(env_now, vd)

        self.csv.writerow([f'{time.time() - self.t0:.3f}'] + [f'{v:.2f}' for v in vals]
                          + [f'{env_now:.1f}', self.state, self.gripper, self.fired])

        for label in self.order:
            self.curves[label].setData(self.tx, self.dtw_hist[label].snapshot())
        self.values.setText('   '.join(f"{label}={v:.0f}" for label, v in zip(self.order, vals))
                            + f"   env={env_now:.0f}")

        color = '#2980b9' if self.gripper == 'open' else '#c0392b'
        ft = f'    <-- {self.fired.upper()} !' if self.fired else ''
        self.gripper_lbl.setText(f'GRIPPER: {self.gripper.upper()}   [{self.state}]{ft}')
        self.gripper_lbl.setStyleSheet(f'background:{color}; color:white;')

        self.tick += 1
        if self.tick % 5 == 0:
            rr = self.raw_ring.snapshot()
            dc = float(np.median(rr)); clip = float(np.mean((rr <= 2) | (rr >= 4093)))
            if clip > 0.02: msg, col = 'CLIPPING / railing', '#c0392b'
            elif dc < 1550: msg, col = 'LOW BIAS (signal/power loss)', '#c0392b'
            elif dc > 2150: msg, col = 'HIGH BIAS (reference loss)', '#c0392b'
            else: msg, col = 'GOOD', '#27ae60'
            self.health.setText(f'HEALTH: {msg}   |   DC={dc:.0f}  clip={clip*100:.0f}%')
            self.health.setStyleSheet(f'background:{col};color:white;')

    def closeEvent(self, e):
        try:
            self.timer.stop()
        except Exception:
            pass
        try:
            self.logf.close()
        except Exception:
            pass
        super().closeEvent(e)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM6')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--fs', type=float, default=200.0)
    ap.add_argument('--mains', type=float, default=50.0, help='Canada: 60')
    ap.add_argument('--seconds', type=float, default=6.0)
    ap.add_argument('--env-max', type=float, default=600.0)
    ap.add_argument('--fs-disp', type=float, default=25.0, help='update/DTW rate (Hz)')
    args = ap.parse_args()
    signal.signal(signal.SIGINT, signal.SIG_IGN)   # immune to stray SIGINT; close via window

    if not os.path.exists(TEMPLATES_PATH):
        print('No templates.json found. Record templates in calibrate.py first.')
        return
    with open(TEMPLATES_PATH) as f:
        data = json.load(f)
    groups = {}   # label -> list of {'n':, 'res':}, ACTIVE templates only
    for t in data.get('templates', []):
        if not t.get('active', True):
            continue
        env = t['env']
        if len(env) >= 2:
            groups.setdefault(t['label'], []).append({'n': len(env), 'res': resample(env, L)})
    if not groups:
        print('No ACTIVE templates in templates.json. Activate some in calibrate.py.')
        return
    order = ([l for l in ['relaxed'] if l in groups]
             + [l for l in groups if l != 'relaxed'])   # relaxed first, then the rest
    print('Active templates per label:', {l: len(groups[l]) for l in order})

    n = int(args.fs * args.seconds); k = max(1, int(0.3 * args.fs))
    sig_ring = Ring(n); env_ring = Ring(n); raw_ring = Ring(k, fill=1850.0)

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except Exception as e:
        print(f'Could not open {args.port}: {e}')
        print('Ports:', [p.device for p in list_ports.comports()])
        print('Close the PlatformIO Serial Monitor first (one program per COM port).')
        return

    reader = SerialReader(ser, args, sig_ring, env_ring, raw_ring); reader.start()

    app = QtWidgets.QApplication(sys.argv)
    win = Monitor(args, env_ring, raw_ring, groups, order); win.resize(1050, 760); win.show()
    try:
        app.exec()
    finally:
        reader.stop(); ser.close()


if __name__ == '__main__':
    main()
