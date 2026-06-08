# Demo Video , Edit Plan (no-voice cut)

You have the footage: (1) screen capture of the live signal, (2) zoom on forearm + electrodes
flexing, (3) the gears turning / gripper opening-closing. This is the EDIT plan to turn those into
a clean ~30 s portfolio clip. No voiceover , captions carry it (also means it works muted, which
is how recruiters watch).

## The one rule
**Show cause → effect in a single beat.** The money moment is: muscle flex → signal spike on
screen → hand moves. If the viewer sees those three things connected once, the whole project lands.
Everything else is framing.

## Sequence (target ~30 s, each shot 2-4 s)
1. **Title card (2 s).** Black or clean background. Text:
   `Can Kadılar , EMG-controlled prosthetic gripper`
   subline: `surface muscle signal → STM32 → 3D-printed hand`
2. **Establish (3 s).** Forearm + electrodes, a calm flex. Caption (lower third):
   `surface EMG, one muscle`
3. **THE BEAT (5-7 s) , the wow.** Split screen (or tight intercut that keeps the timing):
   left = the live signal panel spiking, right = the gripper moving on the same instant.
   Caption: `real-time detection → actuation`
4. **The vocabulary (5-6 s).** Two clean clips: one contraction → caption `1 contraction = close`;
   two contractions → caption `2 = open`. Let the gripper motion read clearly.
5. **(Optional) utility (3-4 s).** Gripper picks up a small object. No caption needed , it speaks.
6. **End card (2-3 s).** `STM32 · bare-metal C · custom involute-gear hand`  +  your site/email.

## Editing choices that make it look "engineer," not "hobbyist"
- **Sync is everything.** Line up the on-screen signal spike with the gripper motion to the frame.
  If they're a beat off, nudge the clips until flex/spike/move land together. That sync is the
  proof it's real.
- **Captions:** clean sans-serif (Inter / Helvetica / SF), lower third, white with a subtle
  shadow, 3-5 words max, appear *with* the action and leave. Never a wall of text. No emojis.
- **Cut on motion.** Start each clip just before the hand moves; cut right after it settles. Dead
  air at the head/tail of clips is what reads "amateur." Tighten ruthlessly.
- **Pacing:** quick but not frantic. ~2-4 s per shot. Total 25-40 s. If in doubt, shorter.
- **Look:** mild contrast + saturation bump so the device pops; make sure the gripper is the
  brightest, sharpest thing in frame. Stabilize any micro-shake. Crop tight , fill the frame with
  the mechanism, not the room.
- **One slow-mo beauty shot is allowed** (the gears meshing as it closes), but keep the proof beat
  (#3) at real speed , real-time *is* the point.
- **Music:** subtle, low, minimal/electronic, mixed quiet , or none. It must still work muted.
  Avoid anything dramatic or cheesy; the work carries it.
- **Aspect:** master in **16:9 1080p** for the site. Export a **1:1 or 9:16** cut too if you want
  to drop it on LinkedIn , that's where engineers will actually see it.
- **Export:** 1080p, H.264, ~30 fps, high bitrate. Keep the project file , this is a reusable
  template; swap in better footage when the project is further along ("demo of demo" → real demo).

## Tools
- **CapCut** , fastest for captions + simple cuts (good for tonight's quick cut).
- **DaVinci Resolve** (free) , if you want finer control / better grading / the split-screen.

## What NOT to do
- No apology, no "this is just a prototype," no explaining the failures , the video shows it
  *working*; the engineering story (DTW evaluated and rejected, root-cause debugging, on-chip port)
  goes in the **written** portfolio entry, not the clip.
- No shaky handheld, no cluttered background, no 2-minute runtime, no uncaptioned silence.
