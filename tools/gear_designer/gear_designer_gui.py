#!/usr/bin/env python3
"""
gear_designer GUI (PyQt6 + pyqtgraph).

Tweak module / teeth / pressure angle / backlash live, see the gear train mesh (with pitch
circles that should touch), read the diameters + center distances, and export DXFs for
SolidWorks. Reuses the generator core from gear_designer.py.

Usage:  python gear_designer_gui.py
"""
import os, sys, signal, math
os.environ.setdefault('PYQTGRAPH_QT_LIB', 'PyQt6')
import numpy as np
from PyQt6 import QtWidgets, QtCore
import pyqtgraph as pg
from gear_designer import gear_outline, info, write_dxf

COLORS = ['#1f77b4', '#e67e22', '#2ca02c', '#9467bd', '#17becf', '#d62728']


def min_teeth_no_undercut(pa_deg):
    return 2.0 / (math.sin(math.radians(pa_deg)) ** 2)


class GearGUI(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle('Gear Designer')
        self.outlines = []
        self.teeth = []

        central = QtWidgets.QWidget(); self.setCentralWidget(central)
        h = QtWidgets.QHBoxLayout(central)

        # left: controls + info + export
        left = QtWidgets.QVBoxLayout()
        form = QtWidgets.QFormLayout()
        self.sb_module = QtWidgets.QDoubleSpinBox(); self.sb_module.setRange(0.2, 10); self.sb_module.setSingleStep(0.1); self.sb_module.setValue(1.2)
        self.sb_pa = QtWidgets.QDoubleSpinBox(); self.sb_pa.setRange(14.5, 30); self.sb_pa.setSingleStep(0.5); self.sb_pa.setValue(20.0)
        self.sb_back = QtWidgets.QDoubleSpinBox(); self.sb_back.setRange(0, 0.6); self.sb_back.setSingleStep(0.02); self.sb_back.setValue(0.10)
        self.sb_bore = QtWidgets.QDoubleSpinBox(); self.sb_bore.setRange(0, 20); self.sb_bore.setSingleStep(0.5); self.sb_bore.setValue(5.0)
        self.ed_teeth = QtWidgets.QLineEdit('34 14 14'); self.ed_teeth.setPlaceholderText('one gear, or a train e.g. 34 14 14')
        form.addRow('module (mm)', self.sb_module)
        form.addRow('pressure angle', self.sb_pa)
        form.addRow('backlash (mm)', self.sb_back)
        form.addRow('bore (mm)', self.sb_bore)
        form.addRow('teeth', self.ed_teeth)
        left.addLayout(form)

        self.info = QtWidgets.QLabel(); self.info.setWordWrap(True)
        self.info.setStyleSheet('font-family: Consolas, monospace;')
        self.info.setAlignment(QtCore.Qt.AlignmentFlag.AlignTop)
        left.addWidget(self.info, 1)

        self.btn_export = QtWidgets.QPushButton('Export DXF(s)...')
        self.btn_export.clicked.connect(self.on_export)
        left.addWidget(self.btn_export)

        wl = QtWidgets.QWidget(); wl.setLayout(left); wl.setFixedWidth(280)
        h.addWidget(wl)

        # right: live preview
        pg.setConfigOption('background', 'w'); pg.setConfigOption('foreground', 'k')
        pg.setConfigOptions(antialias=True)
        self.plot = pg.PlotWidget()
        self.plot.setAspectLocked(True); self.plot.showGrid(x=True, y=True, alpha=0.2)
        h.addWidget(self.plot, 1)

        for w in [self.sb_module, self.sb_pa, self.sb_back]:
            w.valueChanged.connect(self.update_preview)
        self.sb_bore.valueChanged.connect(self.update_preview)
        self.ed_teeth.textChanged.connect(self.update_preview)
        self.update_preview()

    def parse_teeth(self):
        try:
            t = [int(x) for x in self.ed_teeth.text().split()]
            return [z for z in t if z >= 4]
        except ValueError:
            return []

    def update_preview(self):
        self.plot.clear()
        self.teeth = self.parse_teeth()
        m = self.sb_module.value(); pa = self.sb_pa.value(); bl = self.sb_back.value()
        if not self.teeth:
            self.info.setText('enter tooth counts (>=4), e.g. "34 14 14"')
            self.outlines = []
            return
        self.outlines = [gear_outline(m, z, pa, bl) for z in self.teeth]

        cx = 0.0
        lines = [f'module m = {m:.2f} mm   pressure angle {pa:.1f} deg', '']
        zmin = min_teeth_no_undercut(pa)
        for i, (z, o) in enumerate(zip(self.teeth, self.outlines)):
            rp = m * z / 2.0
            oo = o + np.array([cx, 0.0])
            col = COLORS[i % len(COLORS)]
            self.plot.plot(np.append(oo[:, 0], oo[0, 0]), np.append(oo[:, 1], oo[0, 1]),
                           pen=pg.mkPen(col, width=1.5))
            th = np.linspace(0, 2 * math.pi, 120)
            self.plot.plot(cx + rp * np.cos(th), rp * np.sin(th),
                           pen=pg.mkPen('#888888', width=1, style=QtCore.Qt.PenStyle.DashLine))
            d = info(m, z, pa)
            warn = '  <-- UNDERCUT (raise teeth or pressure angle)' if z < zmin else ''
            lines.append(f'z={z:3d}  pitch {d["pitch_dia"]:.2f}  outer {d["outer_dia"]:.2f}{warn}')
            if i < len(self.teeth) - 1:
                cx += m * (z + self.teeth[i + 1]) / 2.0
        if len(self.teeth) > 1:
            lines.append(''); lines.append('center distances (frame):')
            for i in range(len(self.teeth) - 1):
                z1, z2 = self.teeth[i], self.teeth[i + 1]
                lines.append(f'  z{z1} <-> z{z2}:  {m * (z1 + z2) / 2:.2f} mm')
        lines.append('')
        lines.append(f'min teeth (no undercut @ {pa:.0f} deg) = {zmin:.0f}')
        self.info.setText('\n'.join(lines))

    def on_export(self):
        if not self.outlines:
            return
        folder = QtWidgets.QFileDialog.getExistingDirectory(self, 'Export DXFs to folder',
                                                            os.path.dirname(os.path.abspath(__file__)))
        if not folder:
            return
        bore = self.sb_bore.value()
        written = []
        for z, o in zip(self.teeth, self.outlines):
            p = os.path.join(folder, f'gear_z{z}.dxf')
            write_dxf(p, o, bore); written.append(os.path.basename(p))
        QtWidgets.QMessageBox.information(self, 'Exported',
                                          'Wrote:\n' + '\n'.join(sorted(set(written))))


def main():
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    app = QtWidgets.QApplication(sys.argv)
    win = GearGUI(); win.resize(960, 640); win.show()
    app.exec()


if __name__ == '__main__':
    main()
