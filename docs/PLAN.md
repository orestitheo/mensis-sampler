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
- Waveform per track: loop region lit, white playhead. Drag anywhere to select the loop (like selecting
  audio in Live), drag an edge to resize, double-click for the whole sample.
- Pitch (per track, -24..+24 semitones): Warp only; grains read faster/slower. Greyed out in tape mode.
  Param `t1.pitch` (…t4) sits after Bars, so a track is now 9 params and Push banks no longer line up with tracks.
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

## Section 3 notes: Slicing
- On load (message thread) find the hits: energy rise per 5.8 ms frame on a treble-boosted mono mix,
  adaptive threshold, 50 ms minimum gap, then step back to where the hit really starts. Stored in Sample.
- Drum-like guess (many sharp hits that fall back to quiet) turns the track's SLICE on at load; the SLICE
  button overrides. Param `t1.slice` (…t4).
- SLICE on:
  - Chaos Jump lands on a hit; Stutter repeats from the hit before it. Event timing stays on the grid.
  - With Warp: Beats-style playback. Each hit restarts at its own speed exactly when its time comes;
    the previous one fades out in 5 ms, and a hit's tail is gated before it runs into the next hit.
    At a slower tempo a slice is stretched to fill its slot (no gaps): the attack plays untouched, the
    rest with 60 ms grains reading slowly through the slice. Slices under 60 ms play straight.
    Pitch still works. Reverse falls back to grain warp.
  - Fades around Chaos jumps are 1 ms instead of 5 ms so hits keep their attack.
- Waveform shows the slice markers when SLICE is on.

## Section 4 notes: Grain clouds
- Per track, after the looper: a cloud of short grains read straight from the sample, mixed with the loop.
- Where: around the spot the loop is playing (SPRAY widens it, up to +-0.5 s). FREEZE holds the spot
  while the loop moves on.
- Knobs: MIX (dry loop <-> cloud), SIZE (10-500 ms), DENSITY (1-100 grains/s), SPRAY, DETUNE (random
  pitch per grain, up to +-12 st, on top of the track's Pitch), REVERSE (chance a grain plays backwards),
  WIDTH (random pan per grain) + FREEZE. Params `t1.grain.mix` ... (…t4), added after the global
  Chaos params so each track's grains are one Push bank of 8.
- Timing: grain k starts at (k + random 0..1) / density seconds of host time; its random choices are a
  hash of (seed, track, k). Same seed + same song position = same cloud.
- Chaos stirs it a bit: more spray, and sometimes a burst of 3 grains at once.
- Fixed pool of 64 grains per track (steal the most finished one). Sine-squared window, level scaled
  by 1/sqrt(overlap) so dense clouds don't get louder.
- MIX 0 = grains off, no CPU.
- UI: LOOP / GRAINS page buttons switch what the 4 track strips show.

## Section 5 notes: Resonator + morphing filter
- Per track, after the grains: Resonator -> Filter -> level/pan.
- Resonator: a bank of tuned band-pass resonators (TPT/RBJ biquads), 4 partials per note, up to 4 notes.
  - ROOT (C0..C7, Live note names) + CHORD (Unison, Octaves, Fifths, Major, Minor, Sus4, Maj7, Min7).
  - MIDI notes played into Mensis override ROOT/CHORD while held (last 4 held notes, all tracks).
  - COLOR morphs the partial ratios from harmonic 1:2:3:4 (string) to free-bar 1:2.76:5.40:8.93 (bell).
  - DECAY 0.05-10 s sets each resonator's ring time (higher partials ring shorter).
  - Level: each mode is scaled by sqrt(Q) so long decays don't vanish, then a tanh soft clip keeps it
    bounded (a pure tone right on a resonance would otherwise build up a lot). MIX crossfades dry/wet.
  - Retuning glides over ~20 ms; coefficients update every 32 samples.
- Filter: state-variable filter (stable when swept). CUTOFF 20 Hz-20 kHz, RESO, MORPH sweeps
  low-pass -> band-pass -> high-pass. Defaults (LP, 20 kHz, no reso) = bypassed, bit-identical.
- Params per track: `t1.res.mix`, `res.root`, `res.chord`, `res.decay`, `res.color`, `flt.cutoff`,
  `flt.reso`, `flt.morph` (one Push bank of 8). UI: third page RESONATOR.

## Section 6 notes: Color
- Per track, after Resonator/Filter: Drive -> Bits -> Rate -> Squash -> Tilt -> Output, with MIX (dry/wet).
- DRIVE 0..36 dB into a shaper; CHARACTER morphs warm (tanh) -> fuzz (hard clip, pushed harder).
  The shaper uses first-order antiderivative anti-aliasing (ADAA), so hard drive on bright sounds doesn't
  fold back as loud inharmonic junk. Level is compensated so turning DRIVE up doesn't just get louder.
- BITS 16 -> 1 (16 = off), RATE (sample-rate reduction, sample-and-hold, 48 kHz = off down to 200 Hz):
  aliasing here is the point.
- SQUASH: one-knob compressor (threshold, ratio and make-up all from one value), stereo-linked,
  instant attack (catches hits: a squash, not a punch), 50 ms release; make-up keeps -10 dBFS at its level.
- TILT: dark <-> bright, +-6 dB each side of 800 Hz.
- OUTPUT -12..+12 dB, MIX 0..100% (default 100%).
- All neutral = bypassed, bit-identical. Drive/tilt/mix/output are smoothed.
- Params per track: `t1.col.drive`, `col.char`, `col.bits`, `col.rate`, `col.squash`, `col.tilt`, `col.out`,
  `col.mix` (one Push bank of 8). UI: COLOR page.

## Section 7 notes: Space (shared)
- One Space for the whole instrument. Each track has a SEND (`t1.send` ..t4, post level/pan) into it;
  the Space output is added to the mix before master VOLUME.
- Delay: stereo ping-pong (feedback crosses L<->R), time synced to tempo: 1/32 .. 1 bar incl. dotted
  and triplet. FEEDBACK 0..95% with a tone filter and soft saturation in the loop. Time changes glide
  (tape-like) instead of clicking. Up to 6 s.
- Reverb: 8-line feedback delay network (Hadamard mix), 4 input diffusers per side, slowly modulated
  line lengths (smooth, no metallic ring), damping in the loop. SIZE sets room size and decay
  (~0.5 s .. 20 s), TONE sets damping (dark .. bright). The delay echoes also feed the reverb.
- BLEND: delay only <-> reverb only. RETURN: Space output level.
- FREEZE: input to Space muted, delay feedback 100% and reverb lossless: whatever is in there hangs
  forever. Off: decays normally.
- Globals: `space.time`, `space.feedback`, `space.blend`, `space.size`, `space.tone`, `space.return`,
  `space.freeze`. UI: SPACE page with the 4 SENDs and the Space knobs.
- Tracks render into a preallocated scratch buffer so their send can be taken; blocks bigger than
  prepared are processed in chunks.

## BEAT mode (Chaos on sliced tracks)
- Per track BEAT button (`t1.beat` ..t4), needs SLICE. Chaos then works like a drummer remixing the loop,
  always exactly on the grid (FEEL ignored): no jumps/reverse/dropouts. Per step, a seeded coin
  (70% x Chaos) picks an event:
  - Swap: another hit of the loop plays for exactly one step.
  - Roll (from ~20% Chaos): a hit repeats 2 or 4 times per step; 2-step rolls double speed in step two.
    Usually the hit that belongs there, sometimes another one.
- Same seed = same beat; more Chaos only adds events.

## Swing
- Global SWING 50% (straight) .. 75%, on the GRID steps: in each pair of steps the first is stretched by
  (1+s) and the second squeezed by (1-s), s = (swing-50)/50. 66% = triplet shuffle.
- Applied as a time warp before everything else on a track (loop position, Chaos, BEAT), Warp only (in
  tape mode it would wobble the pitch). Beats-style hits use real elapsed time, so swung hits start late
  but play at their own speed and pitch. The grain cloud stays on real time.
