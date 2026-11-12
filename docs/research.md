# Research (Nov 2026)

## Torso S-4 — what inspires us
- 4 stereo tracks. Each runs 5 devices in series, each bypassable:
  - **Material** (source): Tape (record/loop; sync, free or stretch time modes; speed ±3x; glide; overdub
    amount), Poly (8-voice sample playback over MIDI), Bypass (live input).
  - **Granular**: up to 128 grains, ±36 st pitch, free or tempo-synced spawn, scale quantize, pitch
    patterns, random size/rate/detune, 4 s buffer.
  - **Filter**: 48-band tuned resonator bank + regular filter morphing LP → BP → HP, band decay ("ring"),
    scale quantize, dry/wet.
  - **Color**: distortion, compression, bitcrush, noise, tilt EQ.
  - **Space**: delay + reverb in one, freeze.
- Modulation: 4 slots per track. Sources: LFO, Random, ADSR. Shape controls: skew, smooth, distort. Synced or free.
- OS 2.0 added macros, scene snapshots, varispeed recording.
- Tracks can send to each other.

## Tool choices
| Need | Choice | Why |
|---|---|---|
| Framework | JUCE 9.0.5 | Standard C++ audio framework. VST3 + AU + Standalone from one codebase. Free for personal use. |
| Build | CMake + Ninja, CPM for deps | JUCE's native build; deps pinned, nothing vendored. |
| Tests | Catch2 3.9.1 | Standard; used by the Pamplejuce template. |
| Validation | pluginval 1.0.4 | Loads the plugin like a host and stress-tests it. |
| Offline checks | demea-cli + Python | Render and inspect audio without a DAW. |

Not chosen: Rust/nih-plug (no AU, fewer examples), CLAP (not in JUCE 9 yet, Live doesn't need it),
Pamplejuce as a base (signing/IPP/Windows CI we don't need yet; used as a reference only).

## Sources
- https://torsoelectronics.com/pages/s-4
- https://www.soundonsound.com/reviews/torso-electronics-s-4
- https://cdm.link/torso-s-4-os-2-0/
- https://musicradar.com/music-tech/samplers/torso-electronics-s-4-review
- https://forum.juce.com/t/juce-9-is-available-now/69175
- https://github.com/sudara/pamplejuce
- https://github.com/Tracktion/pluginval
