# Demea

A sampler instrument plugin (VST3 + AU) for Ableton Live, inspired by the Torso S-4.
Status: empty skeleton (outputs silence). See `docs/PLAN.md`.

## Build (macOS)
```bash
brew install cmake ninja
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```
The build copies `Demea.vst3` and `Demea.component` into `~/Library/Audio/Plug-Ins/`. Rescan plugins in Live.
