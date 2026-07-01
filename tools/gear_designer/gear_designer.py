#!/usr/bin/env python3
"""
gear_designer : involute spur-gear generator + mesh calculator.

You give it a MODULE (tooth size, shared by all meshing gears) and a tooth count; it
generates a correct involute profile (rolls instead of jamming, unlike triangular teeth)
and exports a DXF you extrude in SolidWorks. For a train, it prints the exact center
distances so the frame is right the first time.

Key rules it enforces for you:
  - all meshing gears share ONE module  m   (pitch_dia = m * teeth)
  - center distance between a pair      = m * (z1 + z2) / 2
  - addendum (tip above pitch) = m,  dedendum (root below pitch) = 1.25 m
  - 3D-print backlash via a small profile shift / tip-relief is added (--backlash)

Usage:
  python gear_designer.py --module 2.0 --teeth 17 --bore 5 --out big.dxf
  python gear_designer.py --module 2.0 --teeth 17 7 7 7    # train: prints center distances
  add --preview to also save a PNG.
"""
import argparse, math
import numpy as np


def gear_outline(module, z, pa_deg=20.0, backlash=0.10, n=18):
    """Return an (N,2) closed outline of an involute spur gear centered at origin (mm)."""
    pa = math.radians(pa_deg)
    rp = module * z / 2.0          # pitch radius
    rb = rp * math.cos(pa)         # base radius
    ra = rp + module               # addendum (outer) radius
    rr = max(rp - 1.25 * module, 0.5)   # dedendum (root) radius

    # involute of the base circle, sampled from base out to the tip
    s_max = math.sqrt(max((ra / rb) ** 2 - 1.0, 0.0))
    s = np.linspace(0.0, s_max, n)
    ix = rb * (np.cos(s) + s * np.sin(s))
    iy = -rb * (np.sin(s) - s * np.cos(s))   # reflected: flank curves IN, narrowing toward the tip

    # rotate the flank symmetric about +x, half tooth thickness at the pitch circle (minus backlash so teeth don't bind)
    s_p = math.sqrt(max((rp / rb) ** 2 - 1.0, 0.0))
    ang_p = math.atan2(-rb * (math.sin(s_p) - s_p * math.cos(s_p)),
                       rb * (math.cos(s_p) + s_p * math.sin(s_p)))
    half = math.pi / (2 * z) - backlash / rp        # backlash thins the tooth slightly
    rot = half - ang_p
    c, sn = math.cos(rot), math.sin(rot)
    rx = ix * c - iy * sn
    ry = ix * sn + iy * c
    right = np.column_stack([rx, ry])               # right flank, base -> tip

    # if the root sits below the base circle, drop a short radial down to the root
    if rr < rb:
        ang0 = math.atan2(right[0, 1], right[0, 0])
        right = np.vstack([[rr * math.cos(ang0), rr * math.sin(ang0)], right])

    left = right.copy(); left[:, 1] *= -1           # mirror -> left flank
    tooth = np.vstack([left, right[::-1]])          # left root->tip, then right tip->root

    out = []
    for k in range(z):
        a = 2 * math.pi * k / z
        ck, sk = math.cos(a), math.sin(a)
        out.append(tooth @ np.array([[ck, -sk], [sk, ck]]).T)
    return np.vstack(out)


def write_dxf(path, outline, bore=0.0):
    """Minimal R12 ASCII DXF: closed polyline outline (+ optional bore circle)."""
    L = ['0', 'SECTION', '2', 'ENTITIES',
         '0', 'POLYLINE', '8', '0', '66', '1', '70', '1']
    for x, y in outline:
        L += ['0', 'VERTEX', '8', '0', '10', f'{x:.4f}', '20', f'{y:.4f}']
    L += ['0', 'SEQEND']
    if bore > 0:
        L += ['0', 'CIRCLE', '8', '0', '10', '0', '20', '0', '40', f'{bore / 2:.4f}']
    L += ['0', 'ENDSEC', '0', 'EOF']
    with open(path, 'w') as f:
        f.write('\n'.join(L) + '\n')


def info(module, z, pa_deg=20.0):
    rp = module * z / 2.0
    return {'pitch_dia': 2 * rp, 'outer_dia': 2 * (rp + module),
            'root_dia': 2 * (rp - 1.25 * module)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--module', type=float, required=True, help='tooth size (mm), shared by mesh')
    ap.add_argument('--teeth', type=int, nargs='+', required=True,
                    help='one gear, or several for a train (prints center distances)')
    ap.add_argument('--pressure-angle', type=float, default=20.0)
    ap.add_argument('--backlash', type=float, default=0.10, help='tooth thinning (mm), for print fit')
    ap.add_argument('--bore', type=float, default=0.0, help='center hole diameter (mm)')
    ap.add_argument('--out', default='gear.dxf')
    ap.add_argument('--preview', action='store_true')
    args = ap.parse_args()

    m = args.module
    print(f'module m = {m} mm,  pressure angle {args.pressure_angle} deg')
    for z in args.teeth:
        d = info(m, z, args.pressure_angle)
        print(f'  z={z:3d}:  pitch {d["pitch_dia"]:.2f}  outer {d["outer_dia"]:.2f}  '
              f'root {d["root_dia"]:.2f} mm')
    if len(args.teeth) > 1:
        print('center distances (mount the frame to these):')
        for i in range(len(args.teeth) - 1):
            z1, z2 = args.teeth[i], args.teeth[i + 1]
            print(f'  z{z1} <-> z{z2}:  {m * (z1 + z2) / 2:.2f} mm')

    outlines = [gear_outline(m, z, args.pressure_angle, args.backlash) for z in args.teeth]
    for z, o in zip(args.teeth, outlines):
        path = args.out if len(args.teeth) == 1 else f'gear_z{z}.dxf'
        write_dxf(path, o, args.bore)
        print(f'wrote {path}  ({len(o)} pts)')

    if args.preview:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(figsize=(7, 7)); ax.set_aspect('equal')
        cx = 0.0
        for i, (z, o) in enumerate(zip(args.teeth, outlines)):
            rp = m * z / 2
            oo = o + np.array([cx, 0.0])
            ax.fill(oo[:, 0], oo[:, 1], alpha=0.35)
            ax.plot(oo[:, 0], oo[:, 1], lw=0.8)
            th = np.linspace(0, 2 * math.pi, 100)
            ax.plot(cx + rp * np.cos(th), rp * np.sin(th), '--', lw=0.6, color='gray')  # pitch circle
            if i < len(args.teeth) - 1:
                cx += m * (z + args.teeth[i + 1]) / 2
        ax.set_title(f'm={m}  teeth={args.teeth}  (dashed = pitch circles, they should touch)')
        fig.savefig('gear_preview.png', dpi=110); print('wrote gear_preview.png')


if __name__ == '__main__':
    main()
