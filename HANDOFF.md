# STM32-EMG-Biosignal , HANDOFF (as of 2026-06-08)

Read this first when continuing in a fresh chat. Repo: `CanGitArchive/STM32-EMG-Biosignal`
(PRIVATE) at `S:\Coding\STM32-EMG-Biosignal`. Owner: Can Kadilar. This is the second
hardware portfolio piece (after the EMG prosthetic-hand thesis): a myoelectric gripper
on real STM32 firmware. Edge-AI / embedded positioning.

## STATE: WORKING end to end (2026-06-08)

Muscle EMG -> on-device DSP -> decision -> slew-limited servo -> 3D-printed involute-gear gripper.
**Current control = a bare RAW-SIGNAL TOGGLE, CONFIRMED working** (`logs/dtw_log_260608-182159.csv`:
12 clean alternating toggles, fast, zero double-fires): when the centered signal dips below
-threshold, toggle open<->close. This is a deliberate RESET after a long classification saga
(amplitude-DTW -> timing-debounce -> blip-counting) that all failed on the full-wave-rectify ECHO
(see Signal-pipeline + Decision sections). DTW is still computed/plotted but drives nothing.
**Two committed checkpoints on main:** `d53ee46` (bare toggle) and the dip-counter (this commit).
**DIP-COUNTING , 1 dip = close, 2 dips = open , CONFIRMED WORKING.** Same proven raw-dip detector,
count dips, decide after a quiet `window` (sb_window 0.5 s) since the last dip. Knobs: `dip < -`
(sb_thr, **425**) and `window s`. CSV has a `sig_min` column (per-tick min of raw) for analysis.
Test `dtw_log_260608-184406.csv`: 4/4 (close,open,close,open). **Threshold validated** by a 71 s
random-gesture test (`...184538.csv`): only a deliberate marker-force crossed -400 (to -522);
deepest *accidental* dip was -394.5, 95% of motion stayed above -122. -425 gives ~30 margin each
side (real contractions reach -456+). Earlier double-miss (`...183837`) was TIMING (dips 0.6 s
apart > 0.55 s window), not threshold , raise `window s` for slower doubles.
**FATIGUE (Can observed):** as he tired, dip depth fell (needed thr 425->350->300->250 to keep
firing). A FIXED threshold is brittle to fatigue (real, documented EMG effect). Fine for a short
demo; the proper future rung for long-session robustness = an ADAPTIVE threshold (track a slow
running estimate of recent dip magnitude, set thr as a fraction of it, so it follows fatigue).
Deliberately NOT built yet , checkpoint-first.
**Read `docs/lessons_learned.md`** , every approach tried this session and why it failed (the
full-wave-rectify echo root cause + the process lessons). Read it before re-touching the control.

## Hardware / wiring

- Nucleo-F446RE (Cortex-M4F). Powers/flashes over **Mini-USB** (not micro). Driver:
  STSW-LINK009 (was needed for the Code 28). Toolchain = **PlatformIO** (`framework =
  stm32cube`, board `nucleo_f446re`). pio CLI: `~/.platformio/penv/Scripts/pio.exe` (NOT on PATH).
- EMG: Grove EMG Detector, analog envelope-ish out -> **PA0 / A0** (ADC1). Power Grove from
  **3V3** (PA0 is not 5V tolerant). Electrodes: red+black on palmaris longus, white ref on wrist.
- Servo: SG90 on **TIM4_CH1 / PB6 (D10)**. Signal->D10, V+->separate 6V pack, GND->battery-
  AND Nucleo GND (shared ground mandatory). Measured: **open 1300us, close 1600us**, clamp
  1270-1620, boots open. SG90 deadband ~1-2deg (small jogs may not register; full moves are fine).
- Gripper: 3D-printed involute-gear pincher. Servo gear -> idler -> 2 finger gears.

## Firmware (`src/main.c`)

Unified: streams raw ADC (PA0) over USART2 at ~200 Hz (one int/line) AND listens for
`S<us>\n` servo commands (RX interrupt). Drives the SG90 with slew-limiting (eases, never
slams) + safe clamp. The EMG DSP + DTW are currently done on the LAPTOP, not on-chip; the
firmware is a dumb streamer + servo. (Porting DTW onto the MCU is a future edge-AI step.)
Flash: `pio run -t upload`.

## Laptop tools (`tools/emg_studio/`, PyQt6 + pyqtgraph)

All GUIs ignore SIGINT (Can's mouse sends stray ones; close via window X). Close the
PlatformIO Serial Monitor before running any (one program per COM port).

- `emg_studio.py` , window 1: live signal + envelope + connection-health banner. Holds the
  SHARED CORE: `Notch`, `Ring` (lock-free), `SerialReader` (per-sample DSP, prime, record hooks).
- `calibrate.py` , window 2: record (relax/open/close) -> trim -> save as named/dated
  templates. Library table: search, filter, **active checkbox** (only active are used),
  **editable name + label** (relabel), **Edit/re-trim selected** (load a saved template back to
  re-trim/relabel and update in place). 3 comparison slots show active templates. Panel-height
  constants at top. **Templates now store `full_env` (whole 4 s capture) + `trim` ([i0,i1])
  alongside `env` (the trimmed slice operate.py reads)** , so re-trim can WIDEN, not just narrow.
  Legacy templates predating this have no `full_env` -> narrow-only until re-recorded.
- `operate.py` , window 3: DTW match + decision FSM + drives the servo. Live tunables.
- `servo_test.py` , manual servo jog (open/close presets, safe band).
- `templates.json` , the template library (each: id/name/label/created/active/env).

## Signal + decision pipeline

Per-sample in `SerialReader`: 50 Hz mains notch biquad -> DC-tracker removal -> **HALF-wave**
rectify (`max(centered,0)`) -> moving-average envelope -> rings. The DC tracker converges FAST
(DC_A_fast 0.05) for the first ~1.5 s after prime, then slow (DC_A=0.001) , else a slightly-off
median seed left the baseline drifting ~8 s at startup and the first pulse landed in that settling
and was missed (`dtw_log_260608-171222.csv`). **THE BIG ONE (2026-06-08, Can found it via the raw
view):** a contraction is one BIPHASIC swing (up then down). Full-wave `abs` folded the down-swing
into a SECOND positive hump , an "echo" , so EVERY contraction read as two humps. THAT was the
root of all the miscounting (the "dropouts"/"bounces"/"rebounds" were echoes, not contact loss).
HALF-wave rectify keeps the up-swing, drops the echo -> one contraction = one clean hump. (If a
contraction reads as ~0 after this, its main swing is NEGATIVE , flip to `max(-centered,0)`.)
**A 20 Hz high-pass was tried 2026-06-08 and
REVERTED** , the Grove EMG Detector's output is already an "envelope-ish" LOW-FREQUENCY signal,
so a 20 Hz HP threw away the band the signal lives in (movements -> "tiny jitter", env became a
slow ~1.5 s sine; log `dtw_log_260608-170405.csv`). The DC-tracker is the correct DC removal for
this sensor. (Generic `Biquad` class + `rbj_highpass` left in the file but UNUSED.) DTW
(operate.py): per label, **min over active templates**, live window = last N samples
(N = template length) resampled to L=40, classic DTW.

**CURRENT STATE (2026-06-08, end of session): operate.py decision is a bare toggle , CONFIRMED
WORKING.** Can called a reset , forget the envelope/FSM entirely and get ONE thing working: in
update_view, if the RAW centered signal (`sig_ring`) dips to <= -threshold (one knob `sb_thr`,
default 400), TOGGLE the gripper (open<->close), one toggle per dip (re-arms when raw climbs back
above -thr). **Test `dtw_log_260608-182159.csv`: 12 clean perfectly-ALTERNATING toggles, including
fast ones ~0.5 s apart, ZERO double-fires.** "Works like a charm, smooth, no matter how fast I
contract." This is the known-good baseline to build up from , the long saga (amplitude-DTW ->
timing-debounce -> blip-counting) all failed because the full-wave-rectify ECHO was lying to them;
the raw-signal toggle sidesteps the envelope entirely.
The blip-counting FSM, step_fsm, _decide_gesture, and the blip/rearm/window/lockout knobs were
REMOVED from operate.py (still in git history + described below for when we build back up). The
3 plots (raw/env/DTW) + CSV log stay. Half-wave rectify stays in emg_studio.py (the envelope is
no longer used for the decision, but it's correct now). Build-up path Can named: get toggle solid,
then "1 dip = close, 2 dips = open." The blip-counter below is the reference for that next rung.

**[SUPERSEDED, kept for reference] Decision FSM , BLIP-COUNTING (per Can's model; replaced the
earlier timing-debounce FSM AND the original amplitude/DTW classification):** A "blip" = env rising above
**blip-peak** (`sb_minpeak`, 300) after having relaxed below **re-arm** (`sb_acton`, 70). IDLE ->
(env>blip-peak) COLLECT [count blips; re-arm whenever env<re-arm; a fresh rise above blip-peak
while armed = another blip] -> after **window** s (`sb_gap`, 0.5) of no new blip -> DECIDE ->
REFRACTORY (lockout + env<re-arm) -> IDLE. DECIDE: **1 blip = close, 2+ = open.** 3 s warm-up. On
FIRE, reprime() flushes the rolling DTW window (now only for the plot). Live tunables (defaults):
**blip peak > 180**, **rearm < 70**, **window s 0.5**, lockout 0.4 s. (blip-peak was 300; lowered
to 180 because Can's taps weakened to 233-310 , `dtw_log_260608-173232.csv` shows a double whose
2nd tap peaked only 233 < 300 so it missed; rebounds stayed <=144, so 180 sits between. blip-peak
is THE knob: set it just below the weakest real tap, above the rebounds.) The re-arm threshold is
NOT a second hurdle for the blip , it only stops one long squeeze from counting as many blips; it
can sit anywhere below blip-peak. **The FSM is now back to this clean form** , the re-arm DWELL and
its `rearm hold s` knob were REMOVED 2026-06-08 once half-wave rectify killed the echo (they were a
band-aid for echo/"dropouts" that no longer exist). With one clean hump per contraction, between
two real contractions env now drops to ~0 (the echo no longer holds it up at ~71-290), so re-arm
at 70 works easily. Re-tune blip-peak by WATCHING the new envelope (now one hump per tap):
set it below the weakest tap, above noise (~150-200 likely). 3rd top panel in operate.py shows the
RAW centered signal for diagnosis. **Logs now write to `tools/emg_studio/logs/`** (gitignored), not
beside the .py files; 65 historical logs moved there (32 were git-tracked , now show as deletions).
**WHY blip-counting beats the timing-debounce it replaced:** blips are separated by AMPLITUDE (env
dipping below re-arm between two peaks), NOT by a timed gap. Can's fast doubles have a deep but
BRIEF dip (~18 env for ~45 ms); the old time-debounce needed the dip to LAST >=split (~0.12 s) so
it merged them into one (false close); the blip counter only needs env to TOUCH below re-arm once,
so the same fast double counts as 2 (verified in sim: fast-double->open, single->close, bumpy
single with dip>re-arm & bump<blip-peak ->close, sub-300 noise->no fire). Removed `sb_split`
(no timed gap anymore). The hysteresis (blip-peak high, re-arm low) is a Schmitt trigger , much
more robust on Can's bouncy signal. **WHY count, not amplitude/DTW:** classifying open/close by
contraction INTENSITY (the original DTW weak-vs-strong) failed because amplitude is the least
reproducible EMG feature (`dtw_log_260608-141530.csv`: same intended close came out env 98 vs 617
in `...141651.csv`). DTW is still computed + plotted (path to a richer SHAPE vocabulary later) but
does not drive the decision; templates stay active only to feed the plot. PENDING hardware test.
**GOTCHA (fixed 2026-06-08 after 1st test):** reprime must NOT reseed `dc`/notch. The first
version reused the startup prime (`dc = v`); firing it mid-contraction seeded dc off the true
bias, and at DC_A=0.001 (~5-15 s time constant) the envelope read garbage for seconds ->
appeared as "random" multi-second cooldowns (log `dtw_log_260608-134831.csv`). Now it clears
rings/MA only, leaving dc+notch running (they already track the baseline). PENDING re-test.

## Gear tool (`tools/gear_designer/`)

`gear_designer.py` (CLI) + `gear_designer_gui.py` (GUI): involute spur-gear generator,
DXF export (for SolidWorks), live mesh preview, center-distance + undercut + bore-vs-root
warnings. Rules: one shared module, center = m(z1+z2)/2, addendum=m, >=14 teeth OR high
pressure angle (30deg lets you go to ~8 teeth). The inverted-tooth-flank bug is FIXED.

## Key learnings (don't relearn)

- EMG noise: 50 Hz mains (plugged), **contact-driven** not random; battery cleaner. Fault
  signatures: low DC = signal/power-side loss, high DC = reference loss, clip = railing.
  Full writeup + plots + data: `docs/emg_noise_findings.md` + `docs/data/`.
- **Startup DC seed must be a MEDIAN of the first ~15 samples, not the first sample** (fixed
  2026-06-08, emg_studio.py). The first serial line after connect is often a fragment ("85"
  out of "1850"); seeding dc off it makes env read garbage (~1600) for ~20 s while it
  reconverges at DC_A=0.001 (log `dtw_log_260608-150817.csv`). Same DC_A-is-slow failure family
  as the reprime-reseed bug.
- **Pulse-COUNTING is fragile to electrode dropouts.** A dying/poorly-stuck electrode makes the
  envelope crash to ~5 mid-contraction; a dropout is indistinguishable from the gap between two
  pulses, so one squeeze gets counted as two (log `dtw_log_260608-151338.csv`: a single close
  split into npulse=2, never fired). Can't be fixed in code (a debounce long enough to bridge a
  ~0.36 s dropout would break real doubles). Mitigations: fresh electrodes + CRISP SHORT pulses
  (quick squeeze-release, not long sustained holds , holds invite mid-contraction dropouts).
- **NOT the electrode , it was the rectification ECHO (CORRECTED 2026-06-08 by Can via the raw
  view).** I spent ~6 iterations blaming "electrode dropouts" for the env crashing mid-contraction
  (375->55->269, `...174215.csv`) and recommending fresh pads. WRONG: the raw signal is clean (no
  bounce). The crash was env passing through the biphasic swing's zero-crossing; the "recovery"
  was the rectified ECHO of the negative lobe. Half-wave rectify fixed it. The whole detour
  (timing-debounce, re-arm dwell, dropout band-aids) was treating that one symptom , now removed.
  **LESSON: when the ENVELOPE looks broken, look at the RAW signal FIRST , don't theorize about
  hardware from the envelope alone.** (operate.py now has a 3rd top panel showing raw centered
  signal for exactly this.)
- **Two OPPOSITE miscount modes seen (`dtw_log_260608-160120.csv`), and the fix for each:**
  (a) single -> false DOUBLE: a post-contraction rebound (env crashed to ~5, bounced to ~48 and
  lingered) crossed the activity gate and was counted as pulse 2. FIXED in code , a sub-pulse now
  counts only if its PEAK >= min-peak (300), so the weak rebound (48) is ignored.
  (b) double -> false SINGLE: the two pulses were ~0.1 s apart (< debounce) so they merged into
  one. NOT a code fix , Can must leave a clear ~0.3-0.5 s gap (relax fully) between the two pulses
  of a double. The two modes overlap on Can's jerky/dropping signal precisely because the
  electrodes are degraded; fresh pads widen the margin.
- **Rectified "bounce" bumps (~70-100) are an ARTIFACT, not pulses (Can, 2026-06-08).** EMG is
  biphasic; the DSP only does slow DC removal (~0.03 Hz HP, kills drift only) with NO real
  high-pass, so a symmetric low-frequency deflection (movement artifact / electrode settling)
  passes through and `abs()` (rectify) folds its negative half into a second positive bump. One
  finger move makes a ~100 bump. The peak-gate (min-peak 300) already keeps these from being
  COUNTED, but they ALSO (log `dtw_log_260608-162943.csv`) hover right at the activity gate (~40-49)
  AFTER a pulse, repeatedly kicking the FSM GAP->PULSE and resetting the gesture-end timer , a
  single took ~3-4 s to fire instead of ~0.6 s. **FIX = raise activity-env gate above the bounce
  (~70), live.** (A 20 Hz high-pass was tried as the "proper" fix and REVERTED , see the pipeline
  section: the Grove sensor's output is already a low-freq envelope, so the HP killed the signal.
  The bounces are a sensor/processing reality here, not removable by filtering; the peak-gate keeps
  them from being counted and the raised activity-env keeps them from resetting the timer.)
- **Templates must be CONSISTENT** length/type (both ~0.7-1 s pulses, differ in INTENSITY
  not length). Inconsistent (short weak open vs long strong close) caused the misfires; the
  Edit/re-trim feature exists to fix that.
- Don't AVERAGE DTW templates (smears the shape); keep individuals, min-match.
- Threshold control failed (adaptive-bias lag on relax + EMG fatigue droop). DTW
  pulse-based is the approach. Threshold version kept as documented baseline in git history.
- Let electrodes settle ~10 min before calibrating (impedance drift).

## OPEN / NEXT items

1. **Refractory / multi-pulse polish , LARGELY DONE.** The pulse-sequence detector (GAP state +
   pulse counting + gesture-end-by-silence) is now the core of the timing-coded FSM (see FSM
   section + item 2). Cooldown history along the way (all 2026-06-08): reprime-on-decide +
   lockout 1.2->0.4 s; fixed a dc-reseed bug in reprime (must clear rings/MA only, never reseed
   dc , else env reads garbage for ~5-15 s); the min-peak gate. The dc-fix is CONFIRMED working;
   the timing FSM is PENDING hardware test.
2. **Stage 2: timing-coded gesture vocabulary** (PIVOTED 2026-06-08 , replaces the amplitude
   "4 grips via strong/weak pulse pairs" idea, which is DROPPED: amplitude coding is fragile
   (logs: same close at env 98 vs 617), and domain experts , Can's exact rehab/prosthetics
   targets , know single-site amplitude is unreliable, so "nobody does it" reads as naive, not
   inventive). New direction keeps the UNIQUE angle (single-channel gesture grammar, not
   2-channel Ottobock on/off) on a RELIABLE feature: single / double / long / triple PULSE
   patterns. **Stage 2a DONE in code** (single=close, double=open, count-based; PENDING test).
   **Stage 2b:** add long-hold / triple for more commands , that's where DTW-over-the-gesture-
   window becomes the genuine classifier (counting alone can't separate long-hold from single).
   CAVEAT for 2b: a long-hold needs a DURATION feature; resampling to fixed L normalizes duration
   away, so DTW shape-match alone treats a wide hump like a narrow one. 2-channel agonist/
   antagonist + proportional control stays available as a LATER axis, NOT needed for a unique +
   robust demo (Can pushed back, rightly, that 2-channel alone = "just Ottobock").
3. **Port the DSP+DTW onto the STM32** (now on laptop) for a standalone edge-AI demo , the
   strongest portfolio line ("classifier on a bare-metal MCU").
4. **Gear math doc** (Can asked): `docs/gripper_mechanics.md` , gear ratio, output
   speed/torque, finger pinch force, servo-angle->aperture kinematics. SG90 specs
   (~1.8 kg-cm stall, 0.1 s/60deg). **Sequencing: do this AFTER Stage 1 polish, BEFORE Stage 2**
   (Can's call 2026-06-08). FINAL printed gear train he gave (2026-06-08):
     - module m = 2.70 mm | pressure angle 28.5 deg | bore 10.00 mm | backlash 0.10 mm
     - z=18: root(inner) 41.85 | pitch 48.60 | outer(tip) 54.00 mm
     - z=9 : root(inner) 17.55 | pitch 24.30 | outer(tip) 29.70 mm   (x2 of these)
     - center distances: z18<->z9 = 36.45 mm ; z9<->z9 = 24.30 mm
     - min teeth (no undercut @ 28 deg) = 9
   Numbers are internally consistent (pitch=mz, tip=m(z+2), root=m(z-2.5), C=m(z1+z2)/2).
   Ratio per mesh: z18<->z9 = 2:1, z9<->z9 = 1:1. CONFIRM the actual topology (handoff says
   servo->idler->2 finger gears, but only 3 gears are dimensioned) before writing kinematics.
5. Demo video + portfolio writeup; CV update around the project (Can flagged the CV update).

## Git / build

Latest commit: `0bd4fdd`. **UNCOMMITTED as of 2026-06-08:** calibrate full-capture re-trim
(calibrate.py + emg_studio.py), cooldown reprime fix (emg_studio.py + operate.py), and the
timing-coded gesture FSM rewrite (operate.py , count-based single/double, per-sub-pulse PEAK
gate at min-peak=300 to reject rebounds, live "split s" debounce tunable, gap-window tunable,
npulse logged), the **blip-counting FSM rewrite** (operate.py , Schmitt-trigger blip detect,
amplitude-separated, removes `sb_split`; tunables now blip-peak/rearm/window/lockout), the startup
median-seed + fast-DC-settle fixes, and `activity env >`/rearm default 70 (Can). (A 20 Hz high-pass
was tried + REVERTED , Grove output is low-freq; `Biquad`/`rbj_highpass` remain unused.) Progress:
amplitude-intensity (DTW) -> timing-debounce -> blip-counting; each fixed the prior's failure mode.
**ROOT CAUSE FOUND 2026-06-08 (Can, via the new raw-signal view): full-wave `abs` was echoing
every biphasic contraction into 2 humps , the source of ALL the miscounting.** Fix = HALF-wave
rectify (emg_studio.py). FSM simplified back to the clean blip counter (re-arm dwell + knob
REMOVED; they were band-aids for the echo). operate.py gained a 3rd top panel (raw centered
signal). The earlier "electrode is the bottleneck" conclusion was WRONG , it was the rectifier.
Logs moved to `logs/` (gitignored); 32 previously-tracked dtw_log_*.csv will commit as deletions.
**Then Can called a RESET (end of session): operate.py decision stripped to a bare raw-signal
TOGGLE (centered <= -sb_thr -> toggle gripper), all FSM/blip/env-decision removed** , a known-good
baseline to rebuild from. Half-wave rectify kept in emg_studio.py. PENDING Can's test of the toggle;
next rung he named = "1 dip = close, 2 dips = open". If a single contraction toggles multiple times,
add a small post-toggle lockout.
Awaiting Can's next run + sign-off before commit. Open optional cleanup: half-wave rectify. Phase status in `docs/phased_plan.md`. Threshold-control +
scope history are in earlier commits.
