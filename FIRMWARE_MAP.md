# Firmware map

Navigation for `src/`. This is the single place that says what each source file does, so the
files themselves stay clean (no big header banners). One file = one part of the device.

Board: NUCLEO-F446RE. Chip: STM32F446RE. Language: C++ (header-only classes, one per part).
Build: PlatformIO (`framework = stm32cube`). Flash: `pio run -t upload`.

## Files (in `src/`)

| File | What it is |
|---|---|
| `main.cpp` | Creates the parts, runs the ~200 Hz loop (servo + telemetry + watchdog), and holds the interrupt handlers: SysTick, USART2, and the 1 kHz ADC/DMA callback where the brain runs. |
| `Emg.h` | `Emg` : the muscle sensor. TIM2 triggers the ADC on PA0 at 1 kHz and DMA parks each result in RAM; `read()` returns the freshest sample (no CPU per reading). |
| `MuscleTrigger.h` | `MuscleTrigger` : the on-chip brain. Tracks the resting baseline live, centers the signal, and returns `true` once per valid flex (a "dip"). Includes the signal-loss failsafe (ignores railing input, freezes the baseline). |
| `Servo.h` | `Servo` : the gripper. PWM on PB6 (TIM4 channel 1). `open()`/`close()`/`toggle()` choose a position; `ease()` (every loop) glides toward it, never slamming; `setRotationSpeed()` sets the step size. |
| `Comms.h` | `Comms` : USB serial to the laptop (USART2). `sendStatus(raw, centered, valid)` streams telemetry as `raw,centered,valid`. (The old `S<us>` receive path is still present but unused now that the chip decides.) |
| `Timer.h` | `Timer` : `waitForNextTick(ms)` paces the loop with no drift; `pause(ms)` is a plain blocking wait. Call `initialLoopTickStarter()` once after `HAL_Init`. |
| `Watchdog.h` | `Watchdog` : the hardware IWDG. `pet()` once per loop; if the loop hangs and stops petting, the chip reboots itself (~2 s). |
| `Notch.h` | `Notch` : a 50 Hz biquad notch; `filter()` on the centered signal kills mains hum (used by MuscleTrigger). |

## The 1 kHz brain + the 200 Hz loop

The brain runs in an interrupt, not the loop. `HAL_ADC_ConvCpltCallback` fires at 1 kHz (every time DMA
has a fresh sample) and runs `trigger.update(emg.read())`: baseline + centering + notch + dip detection.
On a valid flex it sets the `toggleRequested` flag.

The `while(1)` loop, ~200 times a second, just:
1. `emg.read()` / `trigger.centered()` : grab the latest values for telemetry.
2. if `toggleRequested` : `servo.toggle()` and clear the flag.
3. `servo.ease()` : glide one step toward the target.
4. `comms.sendStatus(...)` : stream `raw,centered,valid` to the laptop.
5. `timer.waitForNextTick(5)` : hold the 200 Hz rate.
6. `watchdog.pet()` : reset the ~2 s watchdog.

Interrupt handlers at the bottom of `main.cpp`: `SysTick_Handler` keeps the HAL ms clock; `USART2_IRQHandler`
catches serial bytes; `DMA2_Stream0_IRQHandler` -> `HAL_ADC_ConvCpltCallback` is the 1 kHz brain.

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
| `LOCKOUT` | 125 | samples (~125 ms at 1 kHz) after a release before another flex counts (bounce guard) |
| `RAIL_HIGH` / `RAIL_LOW` | 3000 / 25 | raw outside this band = bad signal (failsafe) |
| `VALID_AFTER` | 1000 | clean samples in a row (~1 s at 1 kHz) needed to trust the signal again |

## Pins

| Part | Pin | Notes |
|---|---|---|
| EMG sensor | PA0 (A0) | ADC1 analog input |
| Servo | PB6 (D10) | TIM4 channel 1, PWM at 50 Hz |
| Serial TX / RX | PA2 / PA3 | USART2, bridged to the one USB cable by the onboard ST-LINK (virtual COM port) |
| Onboard LED | PA5 (LD2) | available, not currently used |
