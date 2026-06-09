# STM32-EMG-Biosignal , HANDOFF (as of 2026-06-09)

Read this when continuing in a fresh chat. Repo: CanGitArchive/STM32-EMG-Biosignal (PRIVATE) at
`S:\Coding\STM32-EMG-Biosignal`. Owner: Can Kadilar. Second hardware portfolio piece (after the
EMG prosthetic-hand thesis): a myoelectric gripper on real STM32 firmware. Edge-AI / embedded.

> GOAL is not in this file. Read first:
> `S:\Coding\ChatAssistants\HealthAssistant\CareerAssistant\STM32_project_goals.md` (the why,
> latest section = Handoff 5) + `STM32_Project_Scope.md` (parts, plan, JD-mapping). And read
> `S:\Coding\CodingPlaybooks\CODING_STYLE_PLAYBOOK.md` (how to work with Can: ONE STEP AT A TIME,
> no autonomous multi-agent / ultracode). File-by-file nav: `FIRMWARE_MAP.md`.

## STATE: on-chip, standalone, VERIFIED (2026-06-09)

The board reads the muscle, does the live signal-centering + the open/close decision ON-CHIP, drives the gripper, and
fails safe, and reboots itself if the code hangs (IWDG watchdog), all standalone. The laptop is just a viewer/logger now.

Firmware = header-only classes in `src/`. Acquisition is hardware (TIM2 -> ADC -> DMA at 1 kHz); the
brain runs in the 1 kHz DMA callback; a 200 Hz super-loop does servo + telemetry + watchdog. (Still no
RTOS , the super-loop is the CURRENT implementation, FreeRTOS is an OPEN deliverable, see below):
- `Emg` (Emg.h): TIM2 triggers the ADC on PA0 at 1 kHz; DMA parks each result in RAM. `read()` returns
  the freshest sample (no CPU per reading).
- `MuscleTrigger` (MuscleTrigger.h): the brain, now run at 1 kHz in the ADC/DMA callback. Live baseline
  tracker (slow EMA, RATE 0.0002), centering (`centered = raw - baseline`) through a 50 Hz notch, dip
  detection (fire once when `centered <= -425` while armed and past the lockout; re-arm above -150;
  LOCKOUT 125 samples ~125 ms at 1 kHz), and the signal-loss failsafe (raw outside [25,3000] = bad;
  invalid until 1000 clean samples ~1 s; while invalid it holds + freezes the baseline).
- `Servo` (Servo.h): SG90 PWM on PB6 / TIM4_CH1; `open()`/`close()`/`toggle()` choose a position,
  `ease()` glides toward it (slew-limited), `setRotationSpeed()` tunes the step.
- `Comms` (Comms.h): USART2 (USB VCP); `sendStatus()` streams "raw,centered,valid". (The old
  `S<us>` receive path is present but unused now the chip decides; candidate for a cleanup pass.)
- `Timer` (Timer.h): `waitForNextTick(5)` = drift-free 200 Hz; `pause(ms)` = blocking delay.
- `Watchdog` (Watchdog.h): hardware IWDG, ~2 s. `pet()` each loop; if the loop hangs and stops petting, the
  chip auto-reboots. Verified 2026-06-09 by a deliberate hang test.
- `main.cpp`: make the objects; the 1 kHz `HAL_ADC_ConvCpltCallback` runs `trigger.update` and flags a
  flex; the 200 Hz loop reads the flag (servo.toggle), eases the servo, streams telemetry, waits the
  tick, pets the watchdog. Handlers: SysTick, USART2, and DMA2_Stream0 -> the ADC callback.

Behavior VERIFIED on hardware (2026-06-09): a flex toggles the gripper, one toggle per flex,
including fast wrist-flicks (Can's low-fatigue method); pulling an electrode -> gripper holds, no
haywire; reattach -> works again after ~1 s. Tunables are named consts at the bottom of
MuscleTrigger.h (Can set LOCKOUT 25 and RAIL_LOW 25 himself).

## How we got here (short)

At the start of THIS (5th) session (2026-06-09, NOT day one of the project) an "ultracode" run
autonomously built a FreeRTOS port before Can could weigh in; it was rolled back entirely for being
autonomous, NOT because FreeRTOS was rejected (see "Known deviations"). Then, one step at a time with Can: refactored the streamer
into his C++ header-only-class style; CP1 ported baseline+centering on-chip (verified: rest ~0, flex
dives to ~-693); CP2 added the on-chip dip detector + toggle + signal-loss failsafe (verified). The
laptop decision is retired; `operate.py` remains only as a laptop toggle fallback.

## Hardware / wiring

- Nucleo-F446RE (Cortex-M4F). Powers/flashes over Mini-USB (not micro). Toolchain = PlatformIO
  (`framework = stm32cube`, board `nucleo_f446re`). pio CLI: `~/.platformio/penv/Scripts/pio.exe`
  (NOT on PATH). Clock = HSI 16 MHz (no SystemClock_Config; deliberate, keeps the validated
  servo/UART/ADC timings, e.g. TIM4 prescaler 16 -> 1 us tick).
- EMG: Grove EMG Detector analog out -> PA0 / A0 (ADC1). Power Grove from 3V3. Electrodes:
  red+black on palmaris longus, white reference on the wrist.
- Servo: SG90 on TIM4_CH1 / PB6 (D10). Signal -> D10, V+ -> separate 6 V pack, GND -> battery AND
  Nucleo GND (shared ground mandatory). Measured: open 1300 us, close 1600 us.
- Gripper: 3D-printed involute-gear pincher (gear math in docs/gripper_mechanics.md).
- CAN (not built yet): MCP2515 over SPI, planned on SPI2 , see docs/wiring.md.

## Laptop tools (tools/emg_studio/, PyQt6 + pyqtgraph)

- `chip_monitor.py` , THE viewer for the current on-chip stream: live centered graph with the
  -425 / -150 lines + a SIGNAL OK/LOST banner + decimated CSV logging to logs/. Run:
  `python chip_monitor.py --port COM6` (close other COM users first; one program per COM port).
  Lag lesson: keep disk I/O OFF the serial-reader thread AND fix the plot X range (`setXRange`,
  auto-range off) or the GUI auto-range churn starves the reader and it rubber-bands. `--antialias`
  for silky lines (slower fullscreen).
- `operate.py` , the old laptop DTW/decision app, now only a toggle fallback. `emg_studio.py`
  (shared serial/DSP core for the OLD 1-value stream) and `calibrate.py` (DTW templates) predate the
  on-chip port. chip_monitor.py is the one that matches the current firmware.

## Build / flash / CI

- Build: `& "C:\Users\kadil\.platformio\penv\Scripts\pio.exe" run -d "S:\Coding\STM32-EMG-Biosignal"`.
  Flash: add `-t upload` (ST-LINK over Mini-USB). CI builds `pio run` on every push.
- Latest commit: see `git log` (do not hardcode a hash here , it goes stale the next commit).

## Known deviations from the original scope (NOT approved cuts , flagged by the 2026-06-09 audit)

The first chat's step list + the Able JD competency table call for these; they are NOT done. An
earlier handoff wrongly recorded the DSP as "done" and FreeRTOS as a settled cut. They are open work,
not closed decisions:
- FreeRTOS / RTOS , the current loop is a super-loop; the "sample / process / comms tasks"
  deliverable (and the JD's RTOS row) is OPEN. Can never decided to drop it.
- DMA + timer-triggered ADC , DONE 2026-06-10: TIM2 hardware-triggers the ADC at 1 kHz and DMA moves
  each sample to RAM (no CPU per reading). The 1 kHz notch + brain run in the DMA-complete callback.
- DSP : a 50 Hz notch biquad is now on-chip (Notch.h, mains rejection, verified 2026-06-09). The
  CMSIS-DSP *library* swap + feature extraction stay optional (the notch demonstrates the DSP box).

## NEXT (Can picks, one at a time)

- CAN (MCP2515 over SPI): broadcast gesture/telemetry; decode on the 24 MHz logic analyzer. Biggest
  remaining JD competency + best demo visual. Needs the MCP2515 module wired.
- IWDG watchdog: DONE 2026-06-09 (Watchdog.h, ~2 s; verified by a deliberate hang test). Both safety halves now in: signal-loss failsafe (bad input) + watchdog (dead code).
- The open competencies above (FreeRTOS, DMA+timer ADC, CMSIS-DSP) if Can wants the full JD spread.
- Ship: README (turn a chip_log CSV into a clean graph) + demo video + CV line (STM32 / on-chip EMG / CAN).

## Reference docs in this repo

- `FIRMWARE_MAP.md` , what each src file is + the detector knobs (the nav doc; no per-file banners, by Can's rule).
- `docs/lessons_learned.md` , the EMG control saga + the half-wave-rectify-echo root cause + the
  "look at the RAW signal before theorizing about hardware" lesson. Read before touching the detector.
- `docs/gripper_mechanics.md` (gear math), `docs/wiring.md`, `docs/emg_noise_findings.md`, `docs/phased_plan.md`.
