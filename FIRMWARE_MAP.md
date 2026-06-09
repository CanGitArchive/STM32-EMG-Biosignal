# Firmware map

Navigation for `src/`. This is the single place that says what each source file does, so the
files themselves stay clean (no big header banners). One file = one part of the device.

Board: NUCLEO-F446RE. Chip: STM32F446RE. Language: C++ (header-only classes, one per part).
Build: PlatformIO (`framework = stm32cube`). Flash: `pio run -t upload`.

## Files (in `src/`)

| File | What it is |
|---|---|
| `main.cpp` | Creates the parts, runs the ~200 Hz loop, and holds the two hardware interrupt handlers (SysTick, USART2). |
| `Emg.h` | `Emg` : the muscle sensor. ADC on PA0. `read()` returns one 0..4095 sample. |
| `MuscleTrigger.h` | `MuscleTrigger` : the on-chip brain. Tracks the resting baseline live, centers the signal, and returns `true` once per valid flex (a "dip"). Includes the signal-loss failsafe (ignores railing input, freezes the baseline). |
| `Servo.h` | `Servo` : the gripper. PWM on PB6 (TIM4 channel 1). `open()`/`close()`/`toggle()` choose a position; `ease()` (every loop) glides toward it, never slamming; `setRotationSpeed()` sets the step size. |
| `Comms.h` | `Comms` : USB serial to the laptop (USART2). `sendStatus(raw, centered, valid)` streams telemetry as `raw,centered,valid`. (The old `S<us>` receive path is still present but unused now that the chip decides.) |
| `Timer.h` | `Timer` : `waitForNextTick(ms)` paces the loop with no drift; `pause(ms)` is a plain blocking wait. Call `initialLoopTickStarter()` once after `HAL_Init`. |

## The loop (`main.cpp`, ~200 times a second)

1. `emg.read()` : take one muscle sample.
2. `trigger.update(raw)` : on-chip baseline + centering + dip detection; returns true on a valid flex.
3. on a flex, `servo.toggle()` : flip the gripper open <-> closed.
4. `servo.ease()` : glide one step toward the target.
5. `comms.sendStatus(raw, centered, trigger.isValid())` : stream telemetry for monitoring.
6. `timer.waitForNextTick(5)` : hold the 200 Hz rate.

The two interrupt handlers at the bottom of `main.cpp`: `SysTick_Handler` keeps the HAL millisecond
clock; `USART2_IRQHandler` feeds each byte to `comms.onByteReceived()` (the now-unused receive path).

## Where the "thinking" is

The decision now runs **on the chip** (`MuscleTrigger`): it tracks the resting baseline, centers the
signal (so the threshold stays correct as the baseline drifts), detects a muscle "dip" past a
threshold, and toggles the gripper, with a failsafe that holds if the signal goes bad (electrode
unplugged / railing). The laptop is just a **viewer** of the `raw,centered,valid` stream (see
`tools/emg_studio/chip_monitor.py`). The board runs standalone.

## On-chip detector knobs (`MuscleTrigger.h`)

| Knob | Value | Meaning |
|---|---|---|
| `DIP_THRESHOLD` | 425 | centered must dip this far below rest to count as a flex |
| `REARM_LEVEL` | 150 | ...and climb back above -this to re-arm for the next flex |
| `LOCKOUT` | 25 | samples (~125 ms) after a release before another flex counts (bounce guard) |
| `RAIL_HIGH` / `RAIL_LOW` | 3000 / 25 | raw outside this band = bad signal (failsafe) |
| `VALID_AFTER` | 200 | clean samples in a row (~1 s) needed to trust the signal again |

## Pins

| Part | Pin | Notes |
|---|---|---|
| EMG sensor | PA0 (A0) | ADC1 analog input |
| Servo | PB6 (D10) | TIM4 channel 1, PWM at 50 Hz |
| Serial TX / RX | PA2 / PA3 | USART2, bridged to the one USB cable by the onboard ST-LINK (virtual COM port) |
| Onboard LED | PA5 (LD2) | available, not currently used |
