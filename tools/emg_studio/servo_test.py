#!/usr/bin/env python3
"""
EMG Studio : manual servo control (gripper bring-up + alignment).

Jog the SG90 by hand to verify wiring, find the safe open/close pulse widths, and align
the printed gripper, all independent of the EMG decoder. Sends "S<us>\\n" to the firmware,
which slew-limits toward it (so it always eases, never slams).

Use this to discover your OPEN_US / CLOSE_US, then those values feed the EMG-driven mode.

Usage:  python servo_test.py --port COM6
Close the PlatformIO Serial Monitor first (one program per COM port).
"""
import sys, signal, argparse, threading
import serial
from serial.tools import list_ports
from PyQt6 import QtWidgets, QtCore

SMIN, SMAX = 800, 2200          # must match the firmware clamp


class ServoTest(QtWidgets.QMainWindow):
    def __init__(self, ser):
        super().__init__()
        self.ser = ser
        self.setWindowTitle('EMG Studio : Manual Servo')
        c = QtWidgets.QWidget(); self.setCentralWidget(c)
        v = QtWidgets.QVBoxLayout(c)

        self.val = QtWidgets.QLabel('1500 us')
        self.val.setAlignment(QtCore.Qt.AlignmentFlag.AlignCenter)
        f = self.val.font(); f.setPointSize(22); f.setBold(True); self.val.setFont(f)
        v.addWidget(self.val)

        self.slider = QtWidgets.QSlider(QtCore.Qt.Orientation.Horizontal)
        self.slider.setRange(SMIN, SMAX); self.slider.setValue(1500)
        self.slider.setTickInterval(100); self.slider.setTickPosition(
            QtWidgets.QSlider.TickPosition.TicksBelow)
        self.slider.valueChanged.connect(self.on_slider)
        v.addWidget(self.slider)

        # nudge + preset buttons
        row = QtWidgets.QHBoxLayout()
        for txt, d in [('-50', -50), ('-10', -10), ('+10', 10), ('+50', 50)]:
            b = QtWidgets.QPushButton(txt); b.clicked.connect(lambda _, dd=d: self.nudge(dd))
            row.addWidget(b)
        v.addLayout(row)

        row2 = QtWidgets.QHBoxLayout()
        for txt, val in [('min %d' % SMIN, SMIN), ('center 1500', 1500), ('max %d' % SMAX, SMAX)]:
            b = QtWidgets.QPushButton(txt); b.clicked.connect(lambda _, vv=val: self.set_us(vv))
            row2.addWidget(b)
        v.addLayout(row2)

        v.addWidget(QtWidgets.QLabel(
            'Jog slowly. Note the us where the gripper is fully OPEN and fully CLOSED\n'
            'without the gears binding, those become OPEN_US / CLOSE_US for EMG mode.'))

        # drain the incoming EMG stream so the OS buffer never overflows
        self._stop = threading.Event()
        threading.Thread(target=self._drain, daemon=True).start()
        self.set_us(1500)

    def _drain(self):
        while not self._stop.is_set():
            try:
                self.ser.read(256)
            except Exception:
                break

    def on_slider(self, v):
        self.val.setText(f'{v} us')
        try:
            self.ser.write(f'S{v}\n'.encode())
        except Exception as e:
            self.val.setText(f'write error: {e}')

    def set_us(self, v):
        v = max(SMIN, min(SMAX, int(v)))
        self.slider.setValue(v)     # triggers on_slider -> sends

    def nudge(self, d):
        self.set_us(self.slider.value() + d)

    def closeEvent(self, e):
        self._stop.set()
        super().closeEvent(e)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM6')
    ap.add_argument('--baud', type=int, default=115200)
    args = ap.parse_args()
    signal.signal(signal.SIGINT, signal.SIG_IGN)   # immune to stray SIGINT; close via window
    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.2)
    except Exception as e:
        print(f'Could not open {args.port}: {e}')
        print('Ports:', [p.device for p in list_ports.comports()])
        print('Close the PlatformIO Serial Monitor first (one program per COM port).')
        return
    app = QtWidgets.QApplication(sys.argv)
    win = ServoTest(ser); win.resize(560, 260); win.show()
    try:
        app.exec()
    finally:
        ser.close()


if __name__ == '__main__':
    main()
