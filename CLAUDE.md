# CLAUDE.md

**Demea**: a sampler instrument plugin (VST3 + AU) for Ableton Live 12 on macOS, inspired by the
Torso S-4 "sculpting sampler". Roadmap: `docs/PLAN.md`. Background: `docs/research.md`.

## Roles and communication
- Oresti is the visionary and does all listening tests. He programs in Python, not C++.
- Claude is the engineer. Write code in ponytail style (smallest complete change).
- Talk to Oresti short, plain and intuitive. Explain C++/DSP ideas in Python terms when that helps.
- When something needs ears, say so and say what to listen for.
- Musical "how should it feel" questions: ask, don't guess.

## Product shape (current)
- One plugin instance = one S-4-style track. 4 tracks = 4 instances in Live.
- Chain: **Source** (sample playback; loop/tape later) → **Grains** → **Resonator** → **Color** → **Space**,
  each bypassable, plus modulators (LFO, Random, Envelope).
- Samples first. Live audio input/recording comes later.
- Use our own device names, not Torso's.

## Stack
C++20, CMake ≥ 3.25 + Ninja, JUCE 9 (pinned tag, via CPM), Catch2 v3, pluginval.
Python (numpy, soundfile, matplotlib) only for offline check/plot scripts in `tools/`.
Don't add dependencies without asking.

## Layout
```
core/      pure DSP + logic. No JUCE plugin/GUI deps (juce_core/juce_audio_basics at most).
plugin/    thin JUCE wrapper: host <-> core. Glue only.
cli/       demea-cli: offline render (no DAW)
tests/     Catch2
tools/     Python scripts; tools/bin/ holds pluginval (gitignored)
testdata/  small WAVs
docs/      plan, research, design notes
```
Everything that makes sound lives in `core/` and can be rendered through `demea-cli`.

## Build, test, validate
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build          # also copies VST3/AU into ~/Library/Audio/Plug-Ins
ctest --test-dir build --output-on-failure
tools/bin/pluginval.app/Contents/MacOS/pluginval --strictness-level 5 --validate-in-process \
  --skip-gui-tests --validate build/plugin/Demea_artefacts/RelWithDebInfo/VST3/Demea.vst3
./build/cli/demea-cli_artefacts/RelWithDebInfo/demea-cli render --seconds 2 --out out/test.wav
```
pluginval download: https://github.com/Tracktion/pluginval/releases (unzip into `tools/bin/`).

## Audio thread rules (processBlock and everything it calls)
- No allocation, no locks, no file/network I/O, no logging.
- Talk to other threads with lock-free FIFOs or atomics. Load/analyse samples off the audio thread,
  then swap the prepared object in atomically.
- Preallocate voice and grain pools in `prepareToPlay`. Fixed max counts, steal when full.
- `juce::ScopedNoDenormals`, no NaN/Inf out, clamp the output. Smooth every continuous parameter.
- Unsure if something allocates? Assume it does.

## Randomness
All randomness takes an explicit seed. Same seed + same input = bit-identical output. Seeds go in plugin state.

## Parameters
- Parameter IDs are stable strings. Never rename once used in Live (automation/presets break).
- Push shows params in banks of 8: put the most important first. Keep total < ~128.

## Testing (Claude can't hear)
1. Unit tests for deterministic DSP.
2. Every render passes `demea::measure` checks (`core/RenderStats.h`): finite, peak, DC.
3. Golden renders for finished features; update only on purpose and say so in the commit.
4. Plot analysis/granular/filter output and look at the PNG before saying it works.
5. pluginval passes before handing a build to Oresti.

## Working style
- One roadmap section at a time. Short design note in `docs/` before a big section.
- One feature per commit, build + tests green before committing.
