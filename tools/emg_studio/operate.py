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
    def __init__(self, args, sig_ring, env_ring, raw_ring, groups, order, ser, reader):
        super().__init__()
        self.args = args
        self.sig_ring = sig_ring
        self.env_ring = env_ring
        self.raw_ring = raw_ring
        self.ser = ser              # serial: write S<us> servo commands on gripper change
        self.reader = reader        # SerialReader: reprime() it on decide to flush the DTW window
        self.open_us = int(args.open_us)
        self.close_us = int(args.close_us)
        self.last_sent = None
        self.groups = groups        # label -> list of {'n':, 'res':} (active templates)
        self.order = order          # label display order
        self.tick = 0
        self.t0 = time.time()

        # rolling history of each label's (min) DTW value, for the traces
        self.hist_n = int(args.fs_disp * args.seconds)
        self.dtw_hist = {label: Ring(self.hist_n, fill=np.nan) for label in order}
        self.tx = np.arange(self.hist_n) / args.fs_disp

        # CSV log -> logs/ subfolder (keep them out of the .py directory)
        stamp = time.strftime('%y%m%d-%H%M%S')
        log_dir = os.path.join(os.path.dirname(TEMPLATES_PATH), 'logs')
        os.makedirs(log_dir, exist_ok=True)
        self.logf = open(os.path.join(log_dir, f'dtw_log_{stamp}.csv'), 'w', newline='')
        self.csv = csv.writer(self.logf)
        self.csv.writerow(['t_s'] + [f"dtw_{label}" for label in order]
                          + ['env', 'sig_min', 'fsm', 'npulse', 'gripper', 'fired'])

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
        self.sb_thr = mkspin(0, 700, 425, 10)       # a "dip" = the raw signal going below -this
        self.sb_window = mkspin(0.1, 2.0, 0.5, 0.1)  # wait this long after the last dip, then decide
        for lbl, w in [('dip < -', self.sb_thr), ('window s', self.sb_window)]:
            ctrl.addWidget(QtWidgets.QLabel(lbl)); ctrl.addWidget(w)
        ctrl.addStretch(1)
        root.addLayout(ctrl)

        # DIP-COUNTING decision: 1 dip = close, 2 dips = open (a dip = raw below -thr; decide after a
        # quiet window since the last dip). Built on the proven raw-dip detector.
        self.gripper = 'open'; self.fired = ''; self.state = 'IDLE'
        self.dip_armed = True; self.npulse = 0; self.quiet = 0   # npulse = dips counted this gesture

        pg.setConfigOption('background', 'w'); pg.setConfigOption('foreground', 'k')
        pg.setConfigOptions(antialias=True)
        glw = pg.GraphicsLayoutWidget(); root.addWidget(glw)

        self.p_sig = glw.addPlot(row=0, col=0)
        self.p_sig.setTitle('RAW signal (centered, pre-rectify) , the envelope is abs()+smooth of THIS')
        self.p_sig.setXRange(0, args.seconds); self.p_sig.showGrid(x=False, y=True, alpha=0.25)
        self.p_sig.enableAutoRange(axis='y')
        self.c_sig = self.p_sig.plot(pen=pg.mkPen('#444444', width=1))
        self.tx_sig = np.arange(sig_ring.n) / args.fs

        self.p_env = glw.addPlot(row=1, col=0)
        self.p_env.setTitle('envelope'); self.p_env.setYRange(0, args.env_max)
        self.p_env.setXRange(0, args.seconds); self.p_env.showGrid(x=False, y=True, alpha=0.25)
        self.p_env.setXLink(self.p_sig)
        self.c_env = self.p_env.plot(pen=pg.mkPen('#e74c3c', width=2))
        self.tx_env = np.arange(env_ring.n) / args.fs

        self.p_dtw = glw.addPlot(row=2, col=0)
        self.p_dtw.setTitle('DTW distance per template  (lower = better match)')
        self.p_dtw.setXRange(0, args.seconds); self.p_dtw.showGrid(x=False, y=True, alpha=0.25)
        self.p_dtw.setLabel('bottom', 'seconds'); self.p_dtw.addLegend(offset=(10, 10))
        self.p_dtw.setXLink(self.p_sig)
        self.curves = {}
        for i, label in enumerate(order):
            self.curves[label] = self.p_dtw.plot(
                pen=pg.mkPen(COLORS[i % len(COLORS)], width=2), name=label)

        self.timer = QtCore.QTimer(); self.timer.timeout.connect(self.update_view)
        self.timer.start(int(1000 / args.fs_disp))

    def update_view(self):
        if self.logf.closed:
            return
        sig_snap = self.sig_ring.snapshot()
        self.c_sig.setData(self.tx_sig, sig_snap)   # raw centered signal , also drives the toggle
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

        # --- 1 dip = close, 2 dips = open (dip-counting on the proven raw-dip detector) ---
        self.fired = ''
        thr = self.sb_thr.value()
        window = max(1, int(self.sb_window.value() * self.args.fs_disp))
        recent_min = float(sig_snap[-12:].min())   # most recent ~60 ms of the raw signal
        new_dip = (recent_min <= -thr) and self.dip_armed
        self.dip_armed = recent_min > -thr          # re-arm once the signal is back above -thr
        if self.state == 'IDLE':
            if new_dip:                             # first dip of a gesture
                self.npulse = 1; self.quiet = 0; self.state = 'COUNTING'
        elif self.state == 'COUNTING':
            self.quiet += 1
            if new_dip:                             # another dip within the window
                self.npulse += 1; self.quiet = 0
            if self.quiet >= window:                # quiet long enough since last dip -> decide
                self.gripper = 'close' if self.npulse == 1 else 'open'
                self.fired = self.gripper; self.state = 'IDLE'

        # drive the gripper when the decision changes it
        if self.gripper != self.last_sent:
            us = self.close_us if self.gripper == 'close' else self.open_us
            try:
                self.ser.write(f'S{us}\n'.encode())
            except Exception:
                pass
            self.last_sent = self.gripper

        self.csv.writerow([f'{time.time() - self.t0:.3f}'] + [f'{v:.2f}' for v in vals]
                          + [f'{env_now:.1f}', f'{recent_min:.1f}', self.state, self.npulse, self.gripper, self.fired])

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
    ap.add_argument('--open-us', type=int, default=1300, help='servo us for OPEN')
    ap.add_argument('--close-us', type=int, default=1600, help='servo us for CLOSE')
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
    win = Monitor(args, sig_ring, env_ring, raw_ring, groups, order, ser, reader); win.resize(1050, 880); win.show()
    try:
        app.exec()
    finally:
        reader.stop(); ser.close()


if __name__ == '__main__':
    main()
