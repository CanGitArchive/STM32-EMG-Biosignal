#!/usr/bin/env python3
# chip_monitor.py : live view + CSV logger for the firmware's "raw,centered,valid" serial stream. Usage: python chip_monitor.py --port COM6
import os, sys, signal, argparse, threading, time, csv
os.environ.setdefault('PYQTGRAPH_QT_LIB', 'PyQt6')
import numpy as np
import serial
from serial.tools import list_ports
from PyQt6 import QtWidgets, QtCore
import pyqtgraph as pg


class Ring:
    # single-writer ring buffer: the reader thread writes, the GUI thread snapshots oldest->newest
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
    # Reads "raw,centered,valid" and pushes centered into the ring; no disk I/O, keeps up with the 200 Hz stream.
    def __init__(self, ser, cen_ring):
        super().__init__(daemon=True)
        self.ser = ser
        self.cen_ring = cen_ring
        self.last_raw = 0
        self.last_cen = 0
        self.valid = 1
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
            self.cen_ring.push(cen)
            self.last_raw, self.last_cen, self.valid = raw, cen, val

    def stop(self):
        self._stop.set()


class Monitor(QtWidgets.QMainWindow):
    def __init__(self, args, cen_ring, reader, logf):
        super().__init__()
        self.reader = reader
        self.cen_ring = cen_ring
        self.logf = logf
        self.csv = csv.writer(logf)
        self.csv.writerow(['t_s', 'raw', 'centered', 'valid'])
        self.t0 = time.time()
        self.frame = 0
        self.tx = np.arange(cen_ring.n) / args.fs

        self.setWindowTitle('On-chip EMG monitor')
        central = QtWidgets.QWidget(); self.setCentralWidget(central)
        lay = QtWidgets.QVBoxLayout(central)

        self.banner = QtWidgets.QLabel('connecting...')
        self.banner.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        f = self.banner.font(); f.setPointSize(14); f.setBold(True); self.banner.setFont(f)
        self.banner.setFixedHeight(46); self.banner.setStyleSheet('background:#555;color:white;')
        lay.addWidget(self.banner)

        pg.setConfigOption('background', 'w'); pg.setConfigOption('foreground', 'k')
        pg.setConfigOptions(antialias=args.antialias)   # off by default: fast even fullscreen
        glw = pg.GraphicsLayoutWidget(); lay.addWidget(glw)

        self.p_cen = glw.addPlot(row=0, col=0)
        self.p_cen.setTitle('centered EMG  (a flex dives below the red line -> the gripper toggles)')
        self.p_cen.setYRange(-args.span, args.span)
        self.p_cen.setXRange(0, args.seconds)   # fix the X range so it never auto-ranges per frame
        self.p_cen.disableAutoRange()           # (the auto-range churn on the threshold lines was the lag)
        self.p_cen.showGrid(x=False, y=True, alpha=0.25)
        self.p_cen.setLabel('bottom', 'seconds')
        self.c_cen = self.p_cen.plot(pen=pg.mkPen('#2980b9', width=2))
        self.p_cen.addLine(y=-args.dip,   pen=pg.mkPen('#c0392b', width=1, style=QtCore.Qt.PenStyle.DashLine))  # dip
        self.p_cen.addLine(y=-args.rearm, pen=pg.mkPen('#e67e22', width=1, style=QtCore.Qt.PenStyle.DashLine))  # re-arm

        self.timer = QtCore.QTimer(); self.timer.timeout.connect(self.update_view); self.timer.start(30)

    def update_view(self):
        self.c_cen.setData(self.tx, self.cen_ring.snapshot())

        # decimated logging: one row per frame (~33 Hz), like operate.py -> no disk-stall lag
        self.csv.writerow([f'{time.time() - self.t0:.3f}',
                           self.reader.last_raw, self.reader.last_cen, self.reader.valid])
        self.frame += 1
        if self.frame % 33 == 0:
            self.logf.flush()

        if self.reader.valid:
            self.banner.setText('SIGNAL OK'); self.banner.setStyleSheet('background:#27ae60;color:white;')
        else:
            self.banner.setText('SIGNAL LOST  -  gripper holding')
            self.banner.setStyleSheet('background:#c0392b;color:white;')

    def closeEvent(self, e):
        try:
            self.timer.stop()
        except Exception:
            pass
        try:
            self.logf.flush()
        except Exception:
            pass
        super().closeEvent(e)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM6')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--fs', type=float, default=50.0, help='samples/sec, MUST match the firmware commsTask rate (now 50 Hz) or the time axis is wrong')
    ap.add_argument('--seconds', type=float, default=5.0, help='width of the scrolling window: lower = flexes look wider and scroll faster, higher = more history on screen')
    ap.add_argument('--span', type=float, default=800.0, help='centered y-range (+/-)')
    ap.add_argument('--dip', type=float, default=425.0, help='dip-threshold line')
    ap.add_argument('--rearm', type=float, default=150.0, help='re-arm line')
    ap.add_argument('--antialias', action='store_true', help='silky lines (prettier, but slower fullscreen)')
    args = ap.parse_args()
    signal.signal(signal.SIGINT, signal.SIG_IGN)   # immune to stray SIGINT; close via the window

    n = int(args.fs * args.seconds)
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

    reader = Reader(ser, cen_ring); reader.start()
    app = QtWidgets.QApplication(sys.argv)
    win = Monitor(args, cen_ring, reader, logf); win.resize(1000, 560); win.show()
    try:
        app.exec()
    finally:
        reader.stop(); ser.close(); logf.close()


if __name__ == '__main__':
    main()
