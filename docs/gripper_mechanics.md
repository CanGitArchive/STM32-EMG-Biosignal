# Gripper Mechanics , Gear Train Math

The drivetrain math for the 3D-printed involute-gear pincer. Gear values are Can's final printed
set (2026-06-08); SG90 specs from the datasheet.

## TWO THINGS TO CONFIRM (then the force/aperture numbers below compute)
1. **Which gear is on the servo shaft?** This doc assumes the **z18 is the input** (servo side),
   driving the two z9 fingers. If the servo actually drives a **z9** instead, the ratio inverts
   (see "If the servo is on a z9" below).
2. **Finger lever length `L`** = distance from a finger gear's centre to its contact/tip point
   (mm). Needed for pinch force and aperture. Measure it off the CAD/print.

## Gear set (printed)
| | teeth z | pitch d | tip (outer) d | root (inner) d |
|---|---|---|---|---|
| input gear | 18 | 48.60 | 54.00 | 41.85 |
| finger gear A | 9 | 24.30 | 29.70 | 17.55 |
| finger gear B | 9 | 24.30 | 29.70 | 17.55 |

Common: module **m = 2.70 mm**, pressure angle **28.5°**, bore 10.00 mm, backlash 0.10 mm.
Sanity (all consistent): pitch = m·z, tip = m(z+2), root = m(z−2.5), centre = m(z₁+z₂)/2.
At 28.5° the no-undercut minimum is ~9 teeth, so the z9 gears are right at the limit (the high
pressure angle is what lets them work at 9 teeth , a deliberate, correct choice).

## Train topology (assumed)
```
  SERVO --(z18)--mesh--(z9, finger A)--mesh--(z9, finger B)
```
- Centre distances: z18↔z9 = **36.45 mm**, z9↔z9 = **24.30 mm** (match the printed frame).
- Fingers A and B **counter-rotate** (z9↔z9, 1:1) , a symmetric pincer. Good.

## Gear ratio (servo → finger)
Servo drives z18; fingers are z9. Driven/driver teeth = 18/9 ⇒
- **Fingers rotate 2× the servo angle** (a 2:1 *step-up*).
- **Finger torque = servo torque ÷ 2.**
- This trades torque for travel: the SG90's limited usable sweep becomes 2× as much finger
  rotation , sensible for a gripper that needs range and has torque to spare.

*If the servo is on a z9 instead:* it becomes a 2:1 *step-down* , fingers move ½ the servo angle,
finger torque = servo torque × 2 (more grip force, less range). Flip the numbers below accordingly.

## Servo sweep → finger sweep
- Pulse band (measured): open **1300 µs**, close **1600 µs** ⇒ **300 µs** of travel.
- SG90 ≈ 0.18°/µs (≈180° over 1000 µs; **calibrate yours** , SG90s vary): 300 µs ≈ **54° servo**.
- Finger sweep = 54° × 2 = **~108° per finger.**

## Speed
- SG90: 0.10 s / 60° = **600°/s** (no load). Finger = ×2 = **1200°/s**.
- 108° finger sweep ≈ **0.09 s** open↔close at full speed. (The firmware slew-limits this on
  purpose , the mechanism is faster than you'd want it to slam.)

## Torque & pinch force
- SG90 stall ≈ **1.8 kgf·cm = 0.176 N·m** at 4.8 V (higher at 6 V , measure if it matters).
- Finger gear torque (÷2) = **0.088 N·m**.
- Fingertip pinch force: `F_tip = 0.088 N·m / (L/1000)` = **88 / L_mm  newtons**.
  - e.g. L = 30 mm ⇒ ~2.9 N · L = 40 mm ⇒ ~2.2 N · L = 50 mm ⇒ ~1.8 N.
  - (That's *stall* / theoretical max; working force is lower. Plenty for light objects, not for
    crushing , consistent with a hobby-servo pincer.)

## Aperture (opening between fingertips)
Each finger sweeps up to ~108°. For symmetric counter-rotating fingers of lever `L`, the tip-to-tip
opening change ≈ `2 · L · sin(Δθ/2)` over the sweep `Δθ` actually used (≤108°). Plug in `L` and the
real open/close angle endpoints to get the mm aperture. (Needs the finger geometry / CAD.)

## One-line summary for the portfolio
"Single SG90 drives a 3D-printed 28.5°-pressure-angle involute spur train (z18→z9→z9), 2:1
step-up to two counter-rotating fingers, ~108° sweep in <0.1 s, ~0.09 N·m per finger."
(Designed with the in-repo `tools/gear_designer` , DXF export, undercut/bore checks.)
