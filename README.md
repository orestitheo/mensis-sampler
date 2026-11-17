# Mensis Sampler

A self-playing sampler plugin (VST3 + AU) for Ableton Live, inspired by the Torso S-4.
4 tracks; each loops a sample in sync with Live. See `docs/PLAN.md`.

![Mensis Sampler](docs/screenshot.png)

## Build (macOS)
```bash
brew install cmake ninja
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```
The build copies `Mensis Sampler.vst3` and `Mensis Sampler.component` into `~/Library/Audio/Plug-Ins/`.
Rescan plugins in Live.

## Offline render
```bash
./build/cli/mensis-cli_artefacts/RelWithDebInfo/mensis-cli loop drums.wav --bpm 120 --bars 2 --out out/loop.wav
```
