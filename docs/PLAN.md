# Plan

Draft roadmap. It will change; we re-plan each section when we reach it.
Mensis Sampler plays itself: samples loop in sync with Live, knobs and Chaos steer them. One instance, 4 tracks.

| # | Section | Done when |
|---|---|---|
| 0 | Research, plan, repo, empty plugin | ✅ Builds, tests + pluginval pass, loads in Live 12 |
| 1 | 4-track looper: load/drop a sample per track, loops synced to Live, Start/Length/Bars/Reverse/Level/Pan | 4 loops in sync and mixable in Live |
| 2 | Chaos + Seed/Re-roll: jumps, stutters, reverse, tape stops, dropouts, no clicks | Chaos 0 = clean, up = wilder; same seed = same result |
| 3 | Slicing: find hits in drums, "Snap to slices" | Chopped beat stays punchy and in time |
| 4 | Grain clouds | Renders + plots look right, no clicks |
| 5 | Resonator: tuned band bank + morphing filter | Frequency plots match targets |
| 6 | Color: drive, crush, compression, tilt | No NaN, no aliasing blowups |
| 7 | Space: delay + reverb + freeze | Tails stable, freeze holds |
| 8 | Modulators (LFO, Random, Envelope), macros | Same seed = same output |
| 9 | Real UI, scenes/snapshots, Push-friendly param banks | Usable from Push |
| later | Live audio input + record/overdub, sends between tracks | — |

## Section 1 notes
- Play position = host beat position, so loops can't drift. The loop region is stretched over N bars
  (tape-style: speed and pitch follow). "Bars" is the speed control that stays in sync.
- Bars is auto-guessed on load from sample length and current tempo.
- 5 ms fade at the loop seam prevents clicks.
- Silent when Live is stopped. Standalone runs on an internal 120 BPM clock.
- Samples load on the message thread and are swapped in via an atomic pointer; old ones are freed only
  after the audio thread has finished a later block.
- Saved with the Live set: params + sample file paths (not the audio itself).
- Warp (default on): keeps the original pitch. Two overlapping 60 ms grains read at normal speed; each new
  grain starts where the tape-style position is, so timing is still locked to Live. Warp off = tape mode.
- Waveform per track: loop region shaded, white playhead, drag near an edge to move Start or End.
- Param IDs: `t1.start`, `t1.length`, `t1.bars`, `t1.reverse`, `t1.warp`, `t1.level`, `t1.pan` (…t4). 28 params, 7 per track.

## Section 2 notes: Chaos
- Global CHAOS (0..1) × per-track CHAOS depth (default 100%, set 0 to keep a track clean).
- Time is cut into grid steps (GRID: 1/32..1/4). At each step a seeded coin flip decides if an event starts.
  FEEL: Grid = events start on the step, Loose = start drifts more as Chaos rises, Free = anywhere in the step.
- No state: every decision is a hash of (seed, track, step), so the same seed + same song position always
  gives the same glitch, even after jumping around in Live.
- Events, in order of appearance as Chaos rises: Jump (play from another step of the loop), Reverse,
  Stutter (repeat a 1, 1/2 or 1/4 step slice), Dropout (silence, 1/8 note max). Each lasts 1, 2 or 4 steps.
  No tape stops: Oresti didn't like the record-player slowdown. Raising Chaos only adds events; the ones you already hear stay.
- Every jump in the audio gets a 5 ms dip (fade out/in) so there are no clicks.
- SEED is a param (saved with the set). RE-ROLL picks a new seed; it takes over at the next bar.
- New param IDs: `chaos`, `chaos.grid`, `chaos.feel`, `seed`, `t1.chaos` (…t4). Track params first
  (8 per track = one Push bank each), global Chaos params in bank 5.
