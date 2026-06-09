#!/usr/bin/env python3
# chip_monitor.py : live view + logger for the on-chip firmware's "raw,centered,valid" stream.
#
# The STM32 now does all the DSP and the decision itself and streams three numbers per line:
#     raw,centered,valid
# This tool only PLOTS them (no laptop-side processing) and logs every sample to a timestamped
# CSV under logs/, so a session can be replayed / turned into graphs for the demo.
#
# Top plot    : raw ADC (~1850 at rest).
# Bottom plot : centered signal, with the dip threshold (-425) and re-arm (-150) lines drawn,
#               so you can watch each flex dive past the trigger.
# Banner      : SIGNAL OK / SIGNAL LOST (the on-chip failsafe's valid flag).
#
# Usage:  python chip_monitor.py --port COM6
# Close the PlatformIO Serial Monitor / other tools first (one program per COM port).
import os, sys, signal, argparse, threading, time, csv
os.environ.setdefault('PYQTGRAPH_QT_LIB', 'PyQt6')
import numpy as np
import serial
from serial.tools import list_ports
from PyQt6 import QtWidgets, QtCore
import pyqtgraph as pg


class Ring:
    # single-writer ring buffer: the reader thread writes, the Qt thread snapshots oldest->newest
    def __init__(self, n, fill=0.0):
        self.n = n
        self.buf = np.full(n, fill, dtype=float)
        self.i = 0

    def push(self, x):
        self.buf[self.i] = x
        self.i = (self.i + 1) % self.n

    def snapshot(self):
        i = self.i
        return np.concatenate((self.buf[i:], self.buf[:i]))


class Reader(threading.Thread):
    # reads "raw,centered,valid" lines, pushes into the plot rings, logs every sample to CSV
    def __init__(self, ser, raw_ring, cen_ring, logf):
        super().__init__(daemon=True)
        self.ser = ser
        self.raw_ring, self.cen_ring = raw_ring, cen_ring
        self.csv = csv.writer(logf)
        self.csv.writerow(['t_s', 'raw', 'centered', 'valid'])
        self.valid = 1
        self.t0 = time.time()
        self._stop = threading.Event()

    def run(self):
        self.ser.reset_input_buffer()
        while not self._stop.is_set():
            try:
                ln = self.ser.readline().decode('ascii', 'ignore').strip()
            except Exception:
                continue
            parts = ln.split(',')
            if len(parts) != 3:
                continue
            try:
                raw, cen, val = int(parts[0]), int(parts[1]), int(parts[2])
            except ValueError:
                continue
            self.raw_ring.push(raw)
            self.cen_ring.push(cen)
            self.valid = val
            self.csv.writerow([f'{time.time() - self.t0:.3f}', raw, cen, val])

    def stop(self):
        self._stop.set()


class Monitor(QtWidgets.QMainWindow):
    def __init__(self, args, raw_ring, cen_ring, reader):
        super().__init__()
        self.reader = reader
        self.raw_ring, self.cen_ring = raw_ring, cen_ring
        self.tx = np.arange(raw_ring.n) / args.fs

        self.setWindowTitle('On-chip EMG monitor')
        central = QtWidgets.QWidget(); self.setCentralWidget(central)
        lay = QtWidgets.QVBoxLayout(central)

        self.banner = QtWidgets.QLabel('connecting...')
        self.banner.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        f = self.banner.font(); f.setPointSize(14); f.setBold(True); self.banner.setFont(f)
        self.banner.setFixedHeight(46); self.banner.setStyleSheet('background:#555;color:white;')
        lay.addWidget(self.banner)

        pg.setConfigOption('background', 'w'); pg.setConfigOption('foreground', 'k')
        pg.setConfigOptions(antialias=True)
        glw = pg.GraphicsLayoutWidget(); lay.addWidget(glw)

        self.p_raw = glw.addPlot(row=0, col=0)
        self.p_raw.setTitle('raw ADC (~1850 at rest)')
        self.p_raw.setYRange(0, 4095); self.p_raw.showGrid(x=False, y=True, alpha=0.25)
        self.c_raw = self.p_raw.plot(pen=pg.mkPen('#888888', width=1))

        self.p_cen = glw.addPlot(row=1, col=0)
        self.p_cen.setTitle('centered signal  (a flex dives below the red line -> the gripper toggles)')
        self.p_cen.setYRange(-args.span, args.span); self.p_cen.showGrid(x=False, y=True, alpha=0.25)
        self.p_cen.setXLink(self.p_raw); self.p_cen.setLabel('bottom', 'seconds')
        self.c_cen = self.p_cen.plot(pen=pg.mkPen('#2980b9', width=2))
        self.p_cen.addLine(y=-args.dip,   pen=pg.mkPen('#c0392b', width=1, style=QtCore.Qt.PenStyle.DashLine))  # dip
        self.p_cen.addLine(y=-args.rearm, pen=pg.mkPen('#e67e22', width=1, style=QtCore.Qt.PenStyle.DashLine))  # re-arm

        self.timer = QtCore.QTimer(); self.timer.timeout.connect(self.update_view); self.timer.start(30)

    def update_view(self):
        self.c_raw.setData(self.tx, self.raw_ring.snapshot())
        self.c_cen.setData(self.tx, self.cen_ring.snapshot())
        if self.reader.valid:
            self.banner.setText('SIGNAL OK'); self.banner.setStyleSheet('background:#27ae60;color:white;')
        else:
            self.banner.setText('SIGNAL LOST  -  gripper holding')
            self.banner.setStyleSheet('background:#c0392b;color:white;')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM6')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--fs', type=float, default=200.0)
    ap.add_argument('--seconds', type=float, default=8.0, help='width of the scrolling window')
    ap.add_argument('--span', type=float, default=800.0, help='centered y-range (+/-)')
    ap.add_argument('--dip', type=float, default=425.0, help='dip-threshold line')
    ap.add_argument('--rearm', type=float, default=150.0, help='re-arm line')
    args = ap.parse_args()
    signal.signal(signal.SIGINT, signal.SIG_IGN)   # immune to stray SIGINT; close via the window

    n = int(args.fs * args.seconds)
    raw_ring = Ring(n, fill=1850.0)
    cen_ring = Ring(n, fill=0.0)

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except Exception as e:
        print(f'Could not open {args.port}: {e}')
        print('Ports:', [p.device for p in list_ports.comports()])
        print('Close the PlatformIO Serial Monitor / other tools first (one program per COM port).')
        return

    log_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, f'chip_log_{time.strftime("%y%m%d-%H%M%S")}.csv')
    logf = open(log_path, 'w', newline='')
    print(f'logging to {log_path}')

    reader = Reader(ser, raw_ring, cen_ring, logf); reader.start()
    app = QtWidgets.QApplication(sys.argv)
    win = Monitor(args, raw_ring, cen_ring, reader); win.resize(1000, 640); win.show()
    try:
        app.exec()
    finally:
        reader.stop(); ser.close(); logf.close()


if __name__ == '__main__':
    main()
