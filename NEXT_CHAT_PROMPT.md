# Paste this whole file into the next chat (STM32 EMG project , chat 5)

You are continuing Can Kadılar's STM32 EMG-gripper project. DO NOT code or guess until you've read
the files below. There are ~200 unrelated "handoff"/"HANDOFF.md" files in this coding space, so use
these EXACT ABSOLUTE PATHS , never search by filename.

## Read these first, in order
1. **GOAL (why the project exists , read FIRST):**
   `S:\Coding\ChatAssistants\HealthAssistant\CareerAssistant\STM32_project_goals.md`
   One-page anchor, reverse-engineered from the **Able Innovations "Embedded Systems Developer" JD**.
   Read its latest "## Handoff" section last , it's the most current.
2. **SCOPE (parts, phased plan, JD-mapping):**
   `S:\Coding\ChatAssistants\HealthAssistant\CareerAssistant\STM32_Project_Scope.md`
3. **BUILD HANDOFF (firmware, tools, control saga, current code):**
   `S:\Coding\STM32-EMG-Biosignal\HANDOFF.md` , read "LATEST DECISIONS" + "CURRENT STATE" near the top.
4. **LESSONS (every approach tried + why it failed , read before touching the control algorithm):**
   `S:\Coding\STM32-EMG-Biosignal\docs\lessons_learned.md`
5. **THESIS (the published prior work this rebuilds , DON'T guess about it):**
   `S:\Coding\PublicRepos\Prosthetic-Hand-Single-EMG-Multi-Pattern\README.md`

## Repo & layout
`S:\Coding\STM32-EMG-Biosignal` (`CanGitArchive/STM32-EMG-Biosignal`, private, `gh` authed).
Firmware = `src/main.c`. Laptop tools = `tools/emg_studio/` (`operate.py` = live control + plots;
`calibrate.py`; `emg_studio.py` = shared serial/DSP core). Logs -> `tools/emg_studio/logs/` (gitignored).
Gear tool = `tools/gear_designer/`. Gear math = `docs/gripper_mechanics.md`. Video edit plan =
`docs/demo_video_plan.md`.

## State in one paragraph
The EMG -> gripper control ALGORITHM is solved and committed (a raw-signal "dip" detector; works
great, two checkpoints on `main`). BUT the firmware is still a dumb USB-streamer + servo driver ,
ALL DSP and the decision run in Python on the laptop. The actual portfolio deliverable , the embedded
competencies (FreeRTOS, on-chip DSP, on-chip classification, CAN, safety) , is NOT built yet. THAT is
the remaining work, and it's the whole point of the project.

## SETTLED decisions , do NOT re-open (this looped across 3 chats):
- **No DTW on the STM32** (the thesis owns DTW). **One gesture only** (dip-counter dropped).
  **Control = single-gesture graded/incremental** (or full toggle). The embedded SYSTEM is the
  deliverable, not the gesture vocabulary.

## NEXT ACTION
Start THE PORT (scope steps 2-7): FreeRTOS scaffold -> on-chip DSP (notch + baseline removal) ->
on-chip single-gesture detector -> CAN (MCP2515 over SPI) -> safety (IWDG watchdog + signal-loss
failsafe) -> ship (CI exists; add demo video). Re-anchor on `STM32_project_goals.md` if scope tempts.

## HARD RULES
- **NEVER give Can time/effort estimates** (he works ~18 h/day, finds them useless). Scope + steps only.
- **No em-dashes or en-dashes** in any Can-facing or public text.
- When you hand off, **append your own "## Handoff 5 (date)" section to `STM32_project_goals.md`**
  (don't edit earlier sections), and update this file + `HANDOFF.md`.
