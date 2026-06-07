#!/usr/bin/env python3
"""
EMG Studio (PyQt6 + pyqtgraph) : live viewer + connection health.

Window 1 of the calibration/operation app. The STM32 streamer firmware sends raw EMG
over serial at ~200 Hz; all DSP, plotting, and health run here.

Design: the reader thread does the DSP ONCE per sample (notch -> center -> rectify ->
smooth) and pushes the results into ring buffers. The Qt timer just displays the stored
arrays. Computing per-sample (not per-frame over the window) means a point never changes
after it is drawn, so the scrolling history stays stable.

Usage:
    python emg_studio.py                      # COM6, 115200, 200 Hz, 50 Hz mains
    python emg_studio.py --port COM6 --mains 60   # Canada

Close the PlatformIO Serial Monitor first : only one program can hold the COM port.
"""
import os, sys, argparse, threading, math
os.environ.setdefault('PYQTGRAPH_QT_LIB', 'PyQt6')
import numpy as np
import serial
from serial.tools import list_ports
from PyQt6 import QtWidgets, QtCore
import pyqtgraph as pg


def rbj_notch(fs, f0, q):
    """Second-order RBJ notch coefficients (b0,b1,b2,a1,a2), a0-normalized."""
    w0 = 2 * math.pi * f0 / fs
    alpha = math.sin(w0) / (2 * q)
    c = math.cos(w0)
    a0 = 1 + alpha
    return 1 / a0, (-2 * c) / a0, 1 / a0, (-2 * c) / a0, (1 - alpha) / a0


class Notch:
    """Stateful direct-form-I biquad, one sample at a time."""
    def __init__(self, fs, f0, q=2.5):
        self.b0, self.b1, self.b2, self.a1, self.a2 = rbj_notch(fs, f0, q)
        self.x1 = self.x2 = self.y1 = self.y2 = 0.0

    def __call__(self, x):
        y = (self.b0 * x + self.b1 * self.x1 + self.b2 * self.x2
             - self.a1 * self.y1 - self.a2 * self.y2)
        self.x2, self.x1 = self.x1, x
        self.y2, self.y1 = self.y1, y
        return y


class Ring:
    """Single-writer ring buffer backed by a numpy array. snapshot() returns oldest->newest.
    Lock-free: the reader thread writes, the Qt thread snapshots; a race only mis-places one
    sample (harmless for display), never raises."""
    def __init__(self, n, fill=0.0):
        self.n = n
        self.buf = np.full(n, fill, dtype=float)
        self.i = 0

    def push(self, x):
        self.buf[self.i] = x
        self.i = (self.i + 1) % self.n

    def fill(self, x):
        self.buf[:] = x

    def snapshot(self):
        i = self.i
        return np.concatenate((self.buf[i:], self.buf[:i]))


class SerialReader(threading.Thread):
    """Reads raw ints, runs the per-sample DSP, pushes into the display/health rings."""
    def __init__(self, ser, args, sig_ring, env_ring, raw_ring):
        super().__init__(daemon=True)
        self.ser = ser
        self.notch = Notch(args.fs, args.mains)
        self.sig_ring, self.env_ring, self.raw_ring = sig_ring, env_ring, raw_ring
        self.W = max(1, int(0.05 * args.fs))     # ~50 ms envelope smoothing
        self.DC_A = 0.001                         # slow display-baseline tracker (~causal, frozen once stored)
        self.dc = 1850.0
        self.sm = np.zeros(self.W); self.si = 0; self.ssum = 0.0
        self._stop = threading.Event()
        self.primed = False

    def run(self):
        self.ser.reset_input_buffer()
        while not self._stop.is_set():
            try:
                ln = self.ser.readline().decode('ascii', 'ignore').strip()
            except Exception:
                continue
            if not ln:
                continue
            try:
                v = float(int(ln))
            except ValueError:
                continue
            if not self.primed:
                self.notch.x1 = self.notch.x2 = self.notch.y1 = self.notch.y2 = v
                self.dc = v
                self.raw_ring.fill(v)
                self.primed = True
                continue
            nt = self.notch(v)
            self.dc += self.DC_A * (nt - self.dc)
            centered = nt - self.dc
            rect = abs(centered)
            self.ssum += rect - self.sm[self.si]
            self.sm[self.si] = rect
            self.si = (self.si + 1) % self.W
            env = self.ssum / self.W
            self.sig_ring.push(centered)
            self.env_ring.push(env)
            self.raw_ring.push(v)

    def stop(self):
        self._stop.set()


class EmgStudio(QtWidgets.QMainWindow):
    def __init__(self, args, sig_ring, env_ring, raw_ring):
        super().__init__()
        self.args = args
        self.sig_ring, self.env_ring, self.raw_ring = sig_ring, env_ring, raw_ring
        self.tx = np.arange(sig_ring.n) / args.fs
        self.tick = 0

        self.setWindowTitle('EMG Studio')
        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        lay = QtWidgets.QVBoxLayout(central)

        self.health = QtWidgets.QLabel('connecting...')
        self.health.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        f = self.health.font(); f.setPointSize(13); f.setBold(True)
        self.health.setFont(f)
        self.health.setFixedHeight(44)
        self.health.setStyleSheet('background:#555; color:white;')
        lay.addWidget(self.health)

        pg.setConfigOption('background', 'w')
        pg.setConfigOption('foreground', 'k')
        pg.setConfigOptions(antialias=True)
        glw = pg.GraphicsLayoutWidget()
        lay.addWidget(glw)

        self.p_sig = glw.addPlot(row=0, col=0)
        self.p_sig.setTitle('signal (centered)')
        self.p_sig.setYRange(-args.sig_max, args.sig_max)
        self.p_sig.setXRange(0, args.seconds)
        self.p_sig.showGrid(x=False, y=True, alpha=0.25)
        self.c_sig = self.p_sig.plot(pen=pg.mkPen('#888888', width=1))

        self.p_env = glw.addPlot(row=1, col=0)
        self.p_env.setTitle('contraction envelope')
        self.p_env.setYRange(0, args.env_max)
        self.p_env.setXLink(self.p_sig)
        self.p_env.showGrid(x=False, y=True, alpha=0.25)
        self.p_env.setLabel('bottom', 'seconds')
        self.c_env = self.p_env.plot(pen=pg.mkPen('#e74c3c', width=2))

        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.update_view)
        self.timer.start(30)   # ~33 fps

    def update_view(self):
        self.c_sig.setData(self.tx, self.sig_ring.snapshot())
        self.c_env.setData(self.tx, self.env_ring.snapshot())

        self.tick += 1
        if self.tick % 6 == 0:   # health ~5x/s
            rr = self.raw_ring.snapshot()
            dc = float(np.median(rr))
            clip = float(np.mean((rr <= 2) | (rr >= 4093)))
            if clip > 0.02:
                msg, col = 'CLIPPING / railing', '#c0392b'
            elif dc < 1550:
                msg, col = 'LOW BIAS (signal / power-side loss)', '#c0392b'
            elif dc > 2150:
                msg, col = 'HIGH BIAS (reference electrode loss)', '#c0392b'
            else:
                msg, col = 'GOOD', '#27ae60'
            self.health.setText(f'HEALTH: {msg}     |     DC={dc:.0f}   clip={clip * 100:.0f}%')
            self.health.setStyleSheet(f'background:{col}; color:white;')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM6')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--fs', type=float, default=200.0)
    ap.add_argument('--mains', type=float, default=50.0, help='Canada: 60')
    ap.add_argument('--seconds', type=float, default=6.0)
    ap.add_argument('--sig-max', type=float, default=1000.0)
    ap.add_argument('--env-max', type=float, default=600.0)
    args = ap.parse_args()

    n = int(args.fs * args.seconds)
    k = max(1, int(0.3 * args.fs))
    sig_ring = Ring(n)
    env_ring = Ring(n)
    raw_ring = Ring(k, fill=1850.0)

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except Exception as e:
        print(f'Could not open {args.port}: {e}')
        print('Ports:', [p.device for p in list_ports.comports()])
        print('Close the PlatformIO Serial Monitor first (one program per COM port).')
        return

    reader = SerialReader(ser, args, sig_ring, env_ring, raw_ring)
    reader.start()

    app = QtWidgets.QApplication(sys.argv)
    win = EmgStudio(args, sig_ring, env_ring, raw_ring)
    win.resize(1000, 640)
    win.show()
    try:
        app.exec()
    finally:
        reader.stop()
        ser.close()


if __name__ == '__main__':
    main()
