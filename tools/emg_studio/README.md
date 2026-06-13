# Laptop viewer

`chip_monitor.py` is the live viewer for the on-chip firmware's serial telemetry. The board runs
standalone (the muscle drives the gripper on-chip); this is a read-only scope + CSV logger, not part
of the control loop.

It plots the centered EMG with the dip-threshold / re-arm lines and a SIGNAL OK / LOST banner (the
on-chip failsafe flag).

## Run

Python 3.12 with `pyserial`, `numpy`, `PyQt6`, `pyqtgraph` (see `requirements.txt`). Close the
PlatformIO Serial Monitor first (one program per COM port), then:

```
python tools/emg_studio/chip_monitor.py --port COM6
```
