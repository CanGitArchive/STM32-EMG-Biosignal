# EMG Studio (laptop tools)

Python-side companion to the STM32 firmware. The firmware (`src/main.c`, streamer build)
sends raw EMG over serial at ~200 Hz; these tools do all the DSP, plotting, recording, and
DTW on the laptop so the algorithm iterates without reflashing.

## Requirements

Python 3 with `pyserial`, `numpy`, `PyQt6`, `pyqtgraph` (all installed here).
Built on PyQt6 + pyqtgraph for smooth real-time rendering (matplotlib's animation
could not keep up with the live stream).

## Windows (built incrementally)

1. **`emg_studio.py`** : live signal + contraction envelope + connection-health banner. DONE.
2. calibration : record relaxed + gesture pulse templates, view + trim them. (next)
3. operation : run DTW live, show gripper open/close + signal-health flags.

(`live_view.py` is the earlier matplotlib prototype, superseded by `emg_studio.py`.)

## Run

Flash the streamer firmware first (`pio run -t upload`), then **close the PlatformIO
Serial Monitor** (only one program can hold the COM port), then:

```
python tools/emg_studio/emg_studio.py --port COM6
```

In Canada (60 Hz mains): add `--mains 60`. Axis ranges: `--sig-max`, `--env-max`.

## Notes

- The 50 Hz notch is computed from the stream rate (`--fs`) and mains (`--mains`), so it
  retargets for 60 Hz automatically.
- DC is removed for display with a windowed median (lag-free), not the slow adaptive bias,
  the relaxed/cooldown DTW template will handle baseline return instead (Can's thesis trick).
