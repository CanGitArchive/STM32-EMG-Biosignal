# Phased plan (living checklist)

Source of truth for scope: `..\..\ChatAssistants\HealthAssistant\CareerAssistant\STM32_Project_Scope.md`.
This file tracks status. Check items off as they land.

> RECONCILED 2026-06-09 against the CURRENT firmware. This checklist was written in the early
> streamer/bring-up era. The Handoff 5 rewrite (commit c79489f) replaced that firmware with the clean
> super-loop and DROPPED several things this file still marked "done" (on-chip timer-ADC@1kHz, 50 Hz
> notch, envelope). Those WERE real in the early firmware (commit a918064) and are recoverable from
> git, but they are NOT in the current super-loop. Marks below corrected to reflect the CURRENT
> firmware; status of record now lives in HANDOFF.md "Known deviations".

Legend: [ ] not started, [~] in progress, [x] done, (HW) needs the board.

Note: the build order was reordered from the scope's numbering to "working core first"
(EMG acquisition + DSP + servo before FreeRTOS/CAN), per Can's preference. Phase numbers
below keep the scope's labels; the checkmarks reflect actual order done.

## Stage : Scaffold
- [x] PlatformIO project (`platformio.ini`, env `nucleo_f446re`, framework `stm32cube`)
- [x] Bare HAL `src/main.c` that compiles (toolchain proof)
- [x] `.gitignore`, README, docs, wiring plan
- [x] GitHub Actions CI (`pio run`)
- [x] Private GitHub repo created + first push green in CI
- [x] Toolchain pre-downloaded locally

## 0. Toolchain (HW)
- [x] Plug in Nucleo over Mini-USB, ST-LINK enumerates (needed STSW-LINK009 driver for Code 28)
- [x] `pio run -t upload` flashes (no ST-LINK firmware upgrade needed)
- [x] Blink LD2 (PA5) : confirmed ~1 Hz 2026-06-05
- [ ] Step through with the ST-LINK debugger : optional, later

## 2. EMG acquisition (HW)  DONE: TIM2-triggered ADC + DMA at 1 kHz
- [x] TIM2 hardware-triggers the ADC at 1 kHz; DMA copies each result into RAM with no CPU (Emg.h).
      Verified 2026-06-10. The early firmware (a918064) had a 1 kHz timer-ISR ADC but no DMA and the
      Handoff 5 rewrite dropped it to polled 200 Hz, so this is restored AND upgraded (DMA, not ISR-poll).
- [x] Read Grove EMG analog envelope (PA0 / A0)
- [x] Serial-plot / log the raw signal; characterized noise (see docs/emg_noise_findings.md)

## 3. DSP  50 Hz notch DONE on-chip (Notch.h); CMSIS-DSP library swap optional
- REMOVED: adaptive bias removal + 50 Hz notch biquad + rectify/moving-average envelope were REAL
      on-chip in the early firmware (commit a918064); the Handoff 5 rewrite (c79489f) dropped them.
      Recoverable from git toward the DSP box.
- [x] 50 Hz mains-rejection notch biquad (Notch.h), on the centered signal in MuscleTrigger. First done
      at 200 Hz (fs/4, a clean 3-term form), then moved to 1 kHz coefficients (the full 5-term biquad,
      20 samples/cycle) when acquisition went to 1 kHz. Verified 2026-06-10: clean line, deep flex dip.
      The baseline + centering + notch now run at 1 kHz in the DMA callback.
- [ ] CMSIS-DSP band-pass / feature extraction : OPEN. (The old "not needed, Grove filters internally"
      framing judges by gripper function; the project exists to DEMONSTRATE the DSP box for the JD,
      the same mistake that mislabeled FreeRTOS. Treat as an open competency.)
- [ ] Feature extraction (RMS/MAV/ZC/WL) : open, ties into the DSP box above.

## 4. Classification
- [ ] Port thesis DTW (2 templates: open / close) + calibration
- [ ] Threshold + hysteresis as the first loop-closer (simpler than DTW; smoke test)
- [ ] Output a discrete gesture class

## 5. Actuation (HW)  servo working
- [x] PWM the SG90 servo on TIM4_CH1 / PB6 (D10); 90 deg sweep confirmed 2026-06-06
- [x] Separate 6 V servo supply, shared ground (no brownout)
- [ ] Drive position from the gesture class (integration step, next)
- [ ] Closed-loop with encoder : not planned (SG90 chosen; see scope)

## 6. CAN (HW)
- [ ] MCP2515 over SPI (controller + transceiver module)
- [ ] Broadcast class / telemetry frames
- [ ] Decode frames on the 24 MHz logic analyzer (PulseView CAN decoder)
- [ ] Optional: SN65HVD230 on native bxCAN as a 2nd node

## 7. Safety  done
- [x] Signal-loss failsafe in the CURRENT firmware (MuscleTrigger.h): railing/unplugged input ->
      gripper holds + baseline frozen, re-trusts after ~1 s clean. (The early health-monitor firmware's
      LD2 warning + auto raw-capture, commit c643855, are NOT in the current super-loop.)
- [x] IWDG watchdog : ~2 s timeout, petted once per loop (Watchdog.h). Verified 2026-06-09 by a
      deliberate hang test (board auto-rebooted out of a forced infinite loop).

## 1. FreeRTOS  (OPEN deliverable, never dropped; the super-loop is only the current implementation)
- [ ] Add FreeRTOS (kernel via middleware or lib_deps)
- [ ] Tasks: sampler / processor / comms + queues
- [ ] Refactor the working super-loop into tasks

## 8. Ship
- [ ] README: scope + results + wiring
- [x] CI green on every push
- [ ] Short demo video
- [ ] Add to portfolio; add STM32/CAN/RTOS to the CV embedded line
