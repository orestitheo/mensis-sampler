# Plan

Draft roadmap. It will change; we re-plan each section when we reach it.

| # | Section | Done when |
|---|---|---|
| 0 | Research, plan, repo, empty plugin | ✅ Builds, tests + pluginval pass, loads in Live 12 |
| 1 | Source, poly mode: load WAV, 8 voices, MIDI note → pitch, ADSR | CLI renders MIDI → WAV; Oresti plays it in Live |
| 2 | Source, loop/tape playback: tempo sync, start/length, speed/varispeed, reverse | Loop stays in sync with Live at any BPM |
| 3 | Grains: granular engine (density, size, pitch, spray, scale quantize) | Renders + plots look right, no clicks |
| 4 | Resonator: tuned band bank + morphing LP/BP/HP filter | Frequency plots match targets |
| 5 | Color: drive, crush, compression, tilt | No NaN, no aliasing blowups |
| 6 | Space: delay + reverb + freeze | Tails stable, freeze holds |
| 7 | Modulation: 4 slots (LFO, Random, Envelope), seeded random, macros | Same seed = same output, bit for bit |
| 8 | UI, scenes/snapshots, Push-friendly param banks | Usable from Push |
| later | Live audio input + record/overdub, multi-track instance, sends | — |
