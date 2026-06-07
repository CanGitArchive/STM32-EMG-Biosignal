#!/usr/bin/env python3
"""
EMG Studio : live view (window 1 of the calibration/operation app).

Reads the raw EMG stream from the STM32 streamer firmware over serial and shows two
live plots: the centered signal and a contraction envelope. ALL DSP runs here (the
firmware just streams raw at ~200 Hz), so we can tune everything without reflashing.

Usage:
    python live_view.py                 # defaults: COM6, 115200, 200 Hz, 50 Hz mains
    python live_view.py --port COM6 --mains 60     # Canada

Close the PlatformIO Serial Monitor first : only one program can hold the COM port.
"""
import argparse, threading, collections, math
import numpy as np
import serial
from serial.tools import list_ports
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation


def rbj_notch(fs, f0, q):
    """Second-order RBJ notch coefficients (b0,b1,b2,a1,a2), a0-normalized."""
    w0 = 2 * math.pi * f0 / fs
    alpha = math.sin(w0) / (2 * q)
    c = math.cos(w0)
    b0, b1, b2 = 1.0, -2 * c, 1.0
    a0, a1, a2 = 1 + alpha, -2 * c, 1 - alpha
    return b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0


class Notch:
    """Stateful direct-form-I biquad, fed one sample at a time."""
    def __init__(self, fs, f0, q=2.5):
        self.b0, self.b1, self.b2, self.a1, self.a2 = rbj_notch(fs, f0, q)
        self.x1 = self.x2 = self.y1 = self.y2 = 0.0

    def __call__(self, x):
        y = (self.b0 * x + self.b1 * self.x1 + self.b2 * self.x2
             - self.a1 * self.y1 - self.a2 * self.y2)
        self.x2, self.x1 = self.x1, x
        self.y2, self.y1 = self.y1, y
        return y


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='COM6')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--fs', type=float, default=200.0, help='stream sample rate (Hz)')
    ap.add_argument('--mains', type=float, default=50.0, help='mains freq (Canada: 60)')
    ap.add_argument('--seconds', type=float, default=6.0, help='visible window length')
    ap.add_argument('--sig-max', type=float, default=1000.0, help='fixed signal y-range (+/-)')
    ap.add_argument('--env-max', type=float, default=600.0, help='fixed envelope y-max')
    args = ap.parse_args()

    N = int(args.fs * args.seconds)
    K = max(1, int(0.3 * args.fs))            # ~300 ms recent window, for cheap health
    notch_buf = collections.deque([0.0] * N, maxlen=N)
    recent_raw = collections.deque([1850.0] * K, maxlen=K)
    notch = Notch(args.fs, args.mains)
    stop = threading.Event()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except Exception as e:
        print(f'Could not open {args.port}: {e}')
        print('Available ports:', [p.device for p in list_ports.comports()])
        print('Tip: close the PlatformIO Serial Monitor (one program per COM port).')
        return

    primed = {'done': False}

    def reader():
        ser.reset_input_buffer()
        while not stop.is_set():
            try:
                ln = ser.readline().decode('ascii', 'ignore').strip()
            except Exception:
                continue
            if not ln:
                continue
            try:
                v = float(int(ln))
            except ValueError:
                continue
            if not primed['done']:
                # prime filter state + buffers with the first real sample so the view
                # starts flat (no cold-start transient, no fill-from-zero blank)
                notch.x1 = notch.x2 = notch.y1 = notch.y2 = v
                notch_buf.extend([v] * N)
                recent_raw.extend([v] * K)
                primed['done'] = True
                continue
            notch_buf.append(notch(v))
            recent_raw.append(v)

    threading.Thread(target=reader, daemon=True).start()

    # --- main live plot : kept lean (no figure text) so it redraws fast and smooth ---
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(11, 6), sharex=True,
                                   num='EMG Studio : live view')
    tx = np.arange(N) / args.fs
    l_sig, = ax1.plot(tx, np.zeros(N), color='gray', lw=0.6)
    ax1.set_ylabel('signal (centered)'); ax1.set_ylim(-args.sig_max, args.sig_max)
    ax1.set_title('signal', fontsize=10)
    l_env, = ax2.plot(tx, np.zeros(N), color='crimson', lw=1.3)
    ax2.set_ylabel('envelope'); ax2.set_xlabel('seconds'); ax2.set_ylim(0, args.env_max)
    ax2.set_title('contraction envelope', fontsize=10)

    # --- separate health window : its own canvas, refreshed ~5x/s, off the plot's hot path ---
    figh = plt.figure('EMG Health', figsize=(5.2, 1.9))
    axh = figh.add_axes([0, 0, 1, 1]); axh.axis('off')
    htext = axh.text(0.5, 0.5, 'connecting...', ha='center', va='center',
                     fontsize=15, fontweight='bold', color='white')

    W = max(1, int(0.05 * args.fs))         # ~50 ms display smoothing
    kern = np.ones(W) / W
    frame = [0]

    def update(_):
        # the plot updates every frame (cheap line redraw -> stays smooth)
        nb = np.asarray(notch_buf, dtype=float)
        centered = nb - np.median(nb)       # lag-free DC estimate over the window
        env = np.convolve(np.abs(centered), kern, mode='same')
        l_sig.set_ydata(centered)
        l_env.set_ydata(env)

        # health refreshes ~5x/s on the OTHER figure, so it never touches the plot canvas
        frame[0] += 1
        if frame[0] % 4 == 0:
            rr = np.asarray(recent_raw, dtype=float)
            dc = float(np.median(rr))
            clip = float(np.mean((rr <= 2) | (rr >= 4093)))
            if clip > 0.02:
                msg, col = 'CLIPPING / railing', '#c0392b'
            elif dc < 1550:
                msg, col = 'LOW BIAS  (signal / power-side loss)', '#c0392b'
            elif dc > 2150:
                msg, col = 'HIGH BIAS  (reference electrode loss)', '#c0392b'
            else:
                msg, col = 'GOOD', '#27ae60'
            htext.set_text(f'{msg}\nDC={dc:.0f}   clip={clip * 100:.0f}%')
            axh.set_facecolor(col)
            figh.canvas.draw_idle()
        return l_sig, l_env

    # keep a reference : without it, matplotlib garbage-collects the animation.
    anim = FuncAnimation(fig, update, interval=50, blit=False, cache_frame_data=False)
    try:
        plt.show()
    finally:
        stop.set(); ser.close()
    _ = anim  # silence linters; the reference above is the functional part


if __name__ == '__main__':
    main()
