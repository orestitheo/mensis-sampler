// mensis-cli: render audio offline (no DAW) so it can be checked and plotted.
// Each new core/ feature adds a flag or subcommand here.
#include <iostream>
#include <map>
#include "core/RenderStats.h"
#include "core/engine/LoopTrack.h"

static int usage() {
    std::cerr << "usage: mensis-cli loop <sample> --out file.wav [--bpm 120] [--bars 1] [--seconds 8]\n"
                 "                  [--start 0] [--length 1] [--reverse 0|1] [--level -6] [--pan 0] [--warp 1] [--rate 48000]\n"
                 "                  [--chaos 0] [--seed 1] [--grid 0.25] [--loose 0] [--pitch 0]\n";
    return 1;
}

static bool writeWav(const juce::AudioBuffer<float>& buffer, double sampleRate, const juce::File& file) {
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr) return false;
    juce::WavAudioFormat wav;
    // 32-bit float so overs (> 0 dBFS) survive in the file and show up in checks.
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
        .withSampleRate(sampleRate).withNumChannels(buffer.getNumChannels()).withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    return writer != nullptr && writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

int main(int argc, char* argv[]) {
    if (argc < 3 || std::string(argv[1]) != "loop") return usage();

    std::map<std::string, std::string> opt{{"--bpm", "120"}, {"--bars", "1"}, {"--seconds", "8"}, {"--start", "0"},
        {"--length", "1"}, {"--reverse", "0"}, {"--level", "-6"}, {"--pan", "0"}, {"--warp", "1"}, {"--chaos", "0"}, {"--seed", "1"}, {"--grid", "0.25"}, {"--loose", "0"}, {"--pitch", "0"}, {"--rate", "48000"}, {"--out", ""}};
    for (int i = 3; i + 1 < argc; i += 2) {
        if (!opt.contains(argv[i])) return usage();
        opt[argv[i]] = argv[i + 1];
    }
    auto num = [&](const char* key) { return std::stod(opt[key]); };
    const double bpm = num("--bpm"), seconds = num("--seconds"), rate = num("--rate");
    if (opt["--out"].empty() || bpm <= 0.0 || seconds <= 0.0 || rate <= 0.0) return usage();

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    auto sample = mensis::loadSample(cwd.getChildFile(argv[2]));
    if (sample == nullptr) { std::cerr << "can't read " << argv[2] << "\n"; return 1; }

    mensis::LoopParams params{static_cast<float>(num("--start")), static_cast<float>(num("--length")),
                                    num("--bars"), num("--reverse") != 0.0,
                                    static_cast<float>(num("--level")), static_cast<float>(num("--pan")),
                                    num("--warp") != 0.0};
    params.chaos.amount = static_cast<float>(num("--chaos"));
    params.chaos.gridBeats = num("--grid");
    params.chaos.looseness = static_cast<float>(num("--loose"));
    params.chaos.seed = params.chaos.nextSeed = static_cast<uint32_t>(num("--seed"));
    params.pitch = static_cast<float>(num("--pitch"));
    mensis::LoopTrack track;
    track.prepare(rate);
    juce::AudioBuffer<float> out(2, static_cast<int>(seconds * rate));
    out.clear();
    constexpr int kBlock = 512; // render in blocks, like a host does
    for (int pos = 0; pos < out.getNumSamples(); pos += kBlock)
        track.render(*sample, params, pos * bpm / (60.0 * rate), bpm, out, pos, std::min(kBlock, out.getNumSamples() - pos));

    if (!writeWav(out, rate, cwd.getChildFile(opt["--out"]))) { std::cerr << "failed writing " << opt["--out"] << "\n"; return 1; }

    bool finite = true;
    float peak = 0.0f, maxStep = 0.0f;
    double dc = 0.0;
    for (int ch = 0; ch < 2; ++ch) {
        const auto s = mensis::measure({out.getReadPointer(ch), static_cast<size_t>(out.getNumSamples())});
        finite = finite && s.finite;
        peak = std::max(peak, s.peak);
        maxStep = std::max(maxStep, s.maxStep);
        dc = std::max(dc, std::abs(s.dcOffset));
    }
    std::cout << "{\"out\": \"" << opt["--out"] << "\", \"finite\": " << (finite ? "true" : "false")
              << ", \"peak\": " << peak << ", \"dc\": " << dc << ", \"maxStep\": " << maxStep << "}\n";
    return finite ? 0 : 2;
}
