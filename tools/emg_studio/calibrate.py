#!/usr/bin/env python3
"""
EMG Studio : calibration + recording manager (window 2).

Record relax / open(weak) / close(strong) pulses, view signal + envelope, drag to trim,
and save as named, dated templates. Manage a library of recordings: search, filter by
label, rename, delete, and toggle which ones are ACTIVE (only active templates are used by
operate.py). Keeping every recording with metadata also lets you mine your strong/weak
captures later. Templates -> templates.json next to this file.

Reuses the serial + DSP core from emg_studio.py.

Usage:  python calibrate.py --port COM6 [--mains 60]
Close the PlatformIO Serial Monitor first (one program per COM port).
"""
import os, sys, json, time, signal, argparse
os.environ.setdefault('PYQTGRAPH_QT_LIB', 'PyQt6')
import numpy as np
import serial
from serial.tools import list_ports
from PyQt6 import QtWidgets, QtCore
import pyqtgraph as pg
from emg_studio import Ring, SerialReader

TEMPLATES_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'templates.json')
SLOT_LABELS = ['relaxed', 'open', 'close']
SLOT_COLORS = ['#1f77b4', '#2ca02c', '#9467bd', '#8c564b', '#17becf', '#bcbd22', '#e377c2']

# --- panel heights in px : tweak these to taste ---
LIVE_H = 230        # live envelope preview
REC_H = 250         # recorded capture / trim
SLOT_H = 360        # the 3 comparison slots
TABLE_MIN_H = 180   # library table minimum (it expands to fill the rest)
WIN_H = 900        # window height


def resample(a, n):
    a = np.asarray(a, dtype=float)
    if len(a) < 2:
        return np.zeros(n)
    return np.interp(np.linspace(0, len(a) - 1, n), np.arange(len(a)), a)


def load_templates():
    data = {'fs': None, 'templates': []}
    if os.path.exists(TEMPLATES_PATH):
        try:
            with open(TEMPLATES_PATH) as f:
                data = json.load(f)
        except Exception:
            pass
    # migrate older entries to the metadata schema
    for i, t in enumerate(data.get('templates', [])):
        t.setdefault('label', 'unnamed')
        t.setdefault('name', t['label'])
        t.setdefault('id', f'legacy-{i}')
        t.setdefault('created', '')
        t.setdefault('active', True)
    return data


def save_templates(data):
    with open(TEMPLATES_PATH, 'w') as f:
        json.dump(data, f, indent=2)


def new_id():
    return time.strftime('%y%m%d-%H%M%S') + '-%03d' % (int(time.time() * 1000) % 1000)


class CalibrateWindow(QtWidgets.QMainWindow):
    def __init__(self, args, env_ring, reader):
        super().__init__()
        self.args = args
        self.env_ring = env_ring
        self.reader = reader
        self.rec_env = None
        self.region = None
        self.editing_id = None      # set when re-trimming an existing template
        self.data = load_templates()
        self.data['fs'] = args.fs
        self._loading = False

        self.setWindowTitle('EMG Studio : Calibration + Library')
        central = QtWidgets.QWidget(); self.setCentralWidget(central)
        root = QtWidgets.QVBoxLayout(central)
        pg.setConfigOption('background', 'w'); pg.setConfigOption('foreground', 'k')
        pg.setConfigOptions(antialias=True)

        # live preview
        self.live = pg.PlotWidget(title='live envelope (watch, then Record)')
        self.live.setYRange(0, args.env_max); self.live.setXRange(0, args.seconds)
        self.live.showGrid(x=False, y=True, alpha=0.25)
        self.c_live = self.live.plot(pen=pg.mkPen('#e74c3c', width=2))
        self.tx_live = np.arange(env_ring.n) / args.fs
        self.live.setMaximumHeight(LIVE_H); root.addWidget(self.live, 0)

        # recorded capture with trim region
        self.rec = pg.PlotWidget(title='recorded capture (drag the shaded edges to trim)')
        self.rec.showGrid(x=False, y=True, alpha=0.25); self.rec.setLabel('bottom', 'seconds')
        self.rec.setMaximumHeight(REC_H); root.addWidget(self.rec, 0)

        # active-template comparison slots
        self.slots = pg.GraphicsLayoutWidget(); self.slots.setFixedHeight(SLOT_H)
        root.addWidget(self.slots, 0)
        self.slot_plots = {}
        for i, label in enumerate(SLOT_LABELS):
            p = self.slots.addPlot(row=0, col=i, title=label)
            p.setYRange(0, args.env_max); p.setXRange(0, 1)
            p.showGrid(x=False, y=True, alpha=0.2)
            self.slot_plots[label] = p

        # record / save controls
        row = QtWidgets.QHBoxLayout()
        row.addWidget(QtWidgets.QLabel('Name:'))
        self.name_edit = QtWidgets.QLineEdit(); self.name_edit.setPlaceholderText('e.g. open firm v2')
        row.addWidget(self.name_edit, 2)
        row.addWidget(QtWidgets.QLabel('Label:'))
        self.label_combo = QtWidgets.QComboBox(); self.label_combo.setEditable(True)
        self.label_combo.addItems(SLOT_LABELS)
        self.label_combo.currentTextChanged.connect(self.refresh_slots)
        row.addWidget(self.label_combo, 1)
        self.record_btn = QtWidgets.QPushButton(f'Record ({int(args.record_seconds)} s)')
        self.record_btn.clicked.connect(self.on_record); row.addWidget(self.record_btn)
        self.save_btn = QtWidgets.QPushButton('Save trimmed'); self.save_btn.setEnabled(False)
        self.save_btn.clicked.connect(self.on_save); row.addWidget(self.save_btn)
        root.addLayout(row)

        # library: search + filter + table + delete
        row2 = QtWidgets.QHBoxLayout()
        row2.addWidget(QtWidgets.QLabel('Search:'))
        self.search = QtWidgets.QLineEdit(); self.search.setPlaceholderText('name or label')
        self.search.textChanged.connect(self.refresh_table); row2.addWidget(self.search, 2)
        row2.addWidget(QtWidgets.QLabel('Filter:'))
        self.filter = QtWidgets.QComboBox(); self.filter.addItems(['all'] + SLOT_LABELS)
        self.filter.currentTextChanged.connect(self.refresh_table); row2.addWidget(self.filter, 1)
        self.edit_btn = QtWidgets.QPushButton('Edit / re-trim selected'); self.edit_btn.clicked.connect(self.on_edit)
        row2.addWidget(self.edit_btn)
        self.del_btn = QtWidgets.QPushButton('Delete selected'); self.del_btn.clicked.connect(self.on_delete)
        row2.addWidget(self.del_btn)
        root.addLayout(row2)

        self.table = QtWidgets.QTableWidget(0, 5)
        self.table.setHorizontalHeaderLabels(['active', 'name', 'label', 'created', 'pts'])
        self.table.horizontalHeader().setStretchLastSection(True)
        self.table.setColumnWidth(0, 55); self.table.setColumnWidth(1, 220)
        self.table.setColumnWidth(2, 80); self.table.setColumnWidth(3, 150)
        self.table.itemChanged.connect(self.on_item_changed)
        self.table.setMinimumHeight(TABLE_MIN_H); root.addWidget(self.table, 1)

        self.refresh_table(); self.refresh_slots()
        self.timer = QtCore.QTimer(); self.timer.timeout.connect(self.update_live); self.timer.start(40)

    # ---- live ----
    def update_live(self):
        self.c_live.setData(self.tx_live, self.env_ring.snapshot())

    # ---- record ----
    def on_record(self):
        self.record_btn.setEnabled(False); self.save_btn.setEnabled(False)
        self.reader.start_record()
        self.countdown = int(self.args.record_seconds)
        self.record_btn.setText(f'Recording... {self.countdown}')
        self.rec_timer = QtCore.QTimer(); self.rec_timer.timeout.connect(self.tick_record); self.rec_timer.start(1000)

    def tick_record(self):
        self.countdown -= 1
        if self.countdown > 0:
            self.record_btn.setText(f'Recording... {self.countdown}'); return
        self.rec_timer.stop()
        rec = self.reader.stop_record()
        self.record_btn.setText(f'Record ({int(self.args.record_seconds)} s)')
        self.record_btn.setEnabled(True)
        self.show_recording(rec)

    def show_recording(self, rec):
        if not rec:
            return
        self.editing_id = None; self.save_btn.setText('Save trimmed')   # a fresh capture appends
        sig = np.array([s for s, e in rec], dtype=float)
        env = np.array([e for s, e in rec], dtype=float)
        self._show_trim(env, sig)

    def _show_trim(self, env, sig=None):
        self.rec_env = env
        t = np.arange(len(env)) / self.args.fs
        self.rec.clear()
        if sig is not None:
            self.rec.plot(t, sig, pen=pg.mkPen('#bbbbbb', width=1))
        self.rec.plot(t, env, pen=pg.mkPen('#e74c3c', width=2))
        self.rec.enableAutoRange()
        self.region = pg.LinearRegionItem([t[0], t[-1]], brush=pg.mkBrush(60, 120, 220, 40))
        self.rec.addItem(self.region)
        self.region.sigRegionChanged.connect(self.refresh_slots)
        self.save_btn.setEnabled(True)
        self.refresh_slots()

    def on_edit(self):
        row = self.table.currentRow()
        if row < 0:
            return
        tid = self.table.item(row, 0).data(QtCore.Qt.ItemDataRole.UserRole)
        t = next((x for x in self.data['templates'] if x['id'] == tid), None)
        if t is None:
            return
        self.editing_id = tid
        self.name_edit.setText(t['name'])
        self.label_combo.setCurrentText(t['label'])
        self.save_btn.setText('Save (update)')
        self._show_trim(np.array(t['env'], dtype=float))   # re-trim the saved envelope

    # ---- save / delete / edit ----
    def on_save(self):
        if self.rec_env is None or self.region is None:
            return
        x0, x1 = self.region.getRegion(); fs = self.args.fs
        i0 = max(0, int(x0 * fs)); i1 = min(len(self.rec_env), int(x1 * fs))
        if i1 - i0 < 2:
            return
        seg = [round(float(x), 2) for x in self.rec_env[i0:i1]]
        label = self.label_combo.currentText().strip() or 'unnamed'
        name = self.name_edit.text().strip() or f'{label} {time.strftime("%H:%M:%S")}'
        if self.editing_id:                     # re-trim: update the existing template
            t = next((x for x in self.data['templates'] if x['id'] == self.editing_id), None)
            if t:
                t['name'] = name; t['label'] = label; t['env'] = seg
            self.editing_id = None; self.save_btn.setText('Save trimmed')
        else:
            self.data['templates'].append({
                'id': new_id(), 'name': name, 'label': label,
                'created': time.strftime('%Y-%m-%d %H:%M:%S'), 'active': True, 'env': seg})
        save_templates(self.data)
        self.name_edit.clear()
        self.refresh_table(); self.refresh_slots()

    def on_delete(self):
        row = self.table.currentRow()
        if row < 0:
            return
        tid = self.table.item(row, 0).data(QtCore.Qt.ItemDataRole.UserRole)
        self.data['templates'] = [t for t in self.data['templates'] if t['id'] != tid]
        save_templates(self.data)
        self.refresh_table(); self.refresh_slots()

    def on_item_changed(self, item):
        if self._loading:
            return
        tid = self.table.item(item.row(), 0).data(QtCore.Qt.ItemDataRole.UserRole)
        t = next((x for x in self.data['templates'] if x['id'] == tid), None)
        if t is None:
            return
        if item.column() == 0:
            t['active'] = item.checkState() == QtCore.Qt.CheckState.Checked
            self.refresh_slots()
        elif item.column() == 1:
            t['name'] = item.text()
        elif item.column() == 2:
            t['label'] = item.text().strip()
            self.refresh_slots()
        save_templates(self.data)

    def refresh_table(self):
        self._loading = True
        q = self.search.text().strip().lower()
        flt = self.filter.currentText()
        rows = [t for t in self.data['templates']
                if (flt == 'all' or t['label'] == flt)
                and (q in t['name'].lower() or q in t['label'].lower())]
        self.table.setRowCount(len(rows))
        for r, t in enumerate(rows):
            chk = QtWidgets.QTableWidgetItem()
            chk.setFlags(QtCore.Qt.ItemFlag.ItemIsUserCheckable | QtCore.Qt.ItemFlag.ItemIsEnabled)
            chk.setCheckState(QtCore.Qt.CheckState.Checked if t['active'] else QtCore.Qt.CheckState.Unchecked)
            chk.setData(QtCore.Qt.ItemDataRole.UserRole, t['id'])
            self.table.setItem(r, 0, chk)
            self.table.setItem(r, 1, QtWidgets.QTableWidgetItem(t['name']))         # editable
            self.table.setItem(r, 2, QtWidgets.QTableWidgetItem(t['label']))        # editable (relabel)
            for c, val in [(3, t.get('created', '')), (4, str(len(t['env'])))]:
                it = QtWidgets.QTableWidgetItem(val)
                it.setFlags(QtCore.Qt.ItemFlag.ItemIsEnabled)                       # read-only
                self.table.setItem(r, c, it)
        self._loading = False

    def refresh_slots(self):
        by_label = {l: [] for l in SLOT_LABELS}
        for t in self.data['templates']:
            if t['label'] in by_label and t.get('active', True):
                by_label[t['label']].append(t['env'])
        cur = self.label_combo.currentText().strip()
        for label, p in self.slot_plots.items():
            p.clear()
            p.setTitle(f'{label}  ({len(by_label[label])} active)')
            for j, env in enumerate(by_label[label]):
                p.plot(np.linspace(0, 1, 50), resample(env, 50),
                       pen=pg.mkPen(SLOT_COLORS[j % len(SLOT_COLORS)], width=2))
            if self.rec_env is not None and self.region is not None and cur == label:
                x0, x1 = self.region.getRegion(); fs = self.args.fs
                i0 = max(0, int(x0 * fs)); i1 = min(len(self.rec_env), int(x1 * fs))
                if i1 - i0 >= 2:
                    p.plot(np.linspace(0, 1, 50), resample(self.rec_env[i0:i1], 50),
                           pen=pg.mkPen('#000000', width=3))   # in-progress capture: bold black


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM6')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--fs', type=float, default=200.0)
    ap.add_argument('--mains', type=float, default=50.0, help='Canada: 60')
    ap.add_argument('--seconds', type=float, default=6.0)
    ap.add_argument('--record-seconds', type=float, default=4.0)
    ap.add_argument('--env-max', type=float, default=600.0)
    args = ap.parse_args()
    signal.signal(signal.SIGINT, signal.SIG_IGN)   # immune to stray SIGINT; close via window

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
    win = CalibrateWindow(args, env_ring, reader); win.resize(1060, WIN_H); win.show()
    try:
        app.exec()
    finally:
        reader.stop(); ser.close()


if __name__ == '__main__':
    main()
