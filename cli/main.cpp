// mensis-cli: render audio offline (no DAW) so it can be checked and plotted.
// Each new core/ feature adds a flag or subcommand here.
#include <iostream>
#include <map>
#include "core/RenderStats.h"
#include "core/engine/LoopTrack.h"
#include "core/engine/Space.h"

static int usage() {
    std::cerr << "usage: mensis-cli loop <sample> --out file.wav [--bpm 120] [--bars 1] [--seconds 8]\n"
                 "                  [--start 0] [--length 1] [--reverse 0|1] [--level -6] [--pan 0] [--warp 1] [--rate 48000]\n"
                 "       mensis-cli slices <sample>   (prints where the hits are, in seconds)\n"
                 "                  [--chaos 0] [--seed 1] [--grid 0.25] [--loose 0] [--pitch 0] [--slice 0|1] [--beat 0|1] [--swing 50]\n"
                 "                  [--grain-mix 0] [--grain-size 80] [--grain-density 20] [--grain-spray 0.2]\n"
                 "                  [--grain-detune 0] [--grain-reverse 0] [--grain-width 0.5]\n"
                 "                  [--res-mix 0] [--res-root 48] [--res-chord 0] [--res-decay 1] [--res-color 0]\n"
                 "                  [--cutoff 20000] [--reso 0] [--morph 0]\n"
                 "                  [--drive 0] [--char 0] [--bits 16] [--crush-rate 48000] [--squash 0] [--tilt 0]\n"
                 "                  [--color-out 0] [--color-mix 1]\n"
                 "                  [--eq-low 0] [--eq-mid 0] [--eq-mid-freq 1000] [--eq-high 0]\n"
                 "                  [--send 0] [--space-time 0.75] [--space-feedback 0.4] [--space-blend 0.5]\n"
                 "                  [--space-size 0.5] [--space-tone 0.5]   (space: the track sent into the shared Space)\n";
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
    if (argc == 3 && std::string(argv[1]) == "slices") {
        auto sample = mensis::loadSample(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]));
        if (sample == nullptr) { std::cerr << "can't read " << argv[2] << "\n"; return 1; }
        std::cout << "{\"drumLike\": " << (sample->drumLike ? "true" : "false") << ", \"seconds\": [";
        for (size_t i = 0; i < sample->slices.size(); ++i)
            std::cout << (i ? ", " : "") << sample->slices[i] / sample->sampleRate;
        std::cout << "]}\n";
        return 0;
    }
    if (argc < 3 || std::string(argv[1]) != "loop") return usage();

    std::map<std::string, std::string> opt{{"--bpm", "120"}, {"--bars", "1"}, {"--seconds", "8"}, {"--start", "0"},
        {"--length", "1"}, {"--reverse", "0"}, {"--level", "-6"}, {"--pan", "0"}, {"--warp", "1"}, {"--chaos", "0"}, {"--seed", "1"}, {"--grid", "0.25"}, {"--loose", "0"}, {"--pitch", "0"}, {"--slice", "0"}, {"--beat", "0"}, {"--swing", "50"}, {"--grain-mix", "0"}, {"--grain-size", "80"}, {"--grain-density", "20"},
        {"--grain-spray", "0.2"}, {"--grain-detune", "0"}, {"--grain-reverse", "0"}, {"--grain-width", "0.5"},
        {"--res-mix", "0"}, {"--res-root", "48"}, {"--res-chord", "0"}, {"--res-decay", "1"}, {"--res-color", "0"},
        {"--cutoff", "20000"}, {"--reso", "0"}, {"--morph", "0"},
        {"--drive", "0"}, {"--char", "0"}, {"--bits", "16"}, {"--crush-rate", "48000"}, {"--squash", "0"}, {"--tilt", "0"},
        {"--color-out", "0"}, {"--color-mix", "1"},
        {"--eq-low", "0"}, {"--eq-mid", "0"}, {"--eq-mid-freq", "1000"}, {"--eq-high", "0"},
        {"--send", "0"}, {"--space-time", "0.75"}, {"--space-feedback", "0.4"}, {"--space-blend", "0.5"},
        {"--space-size", "0.5"}, {"--space-tone", "0.5"}, {"--rate", "48000"}, {"--out", ""}};
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
    params.slice = num("--slice") != 0.0;
    params.beat = num("--beat") != 0.0;
    params.swing = (num("--swing") - 50.0) / 50.0;
    params.grains = {static_cast<float>(num("--grain-mix")), static_cast<float>(num("--grain-size")),
                     static_cast<float>(num("--grain-density")), static_cast<float>(num("--grain-spray")),
                     static_cast<float>(num("--grain-detune")), static_cast<float>(num("--grain-reverse")),
                     static_cast<float>(num("--grain-width"))};
    params.grains.chaos = params.chaos.amount;
    params.grains.seed = params.chaos.seed;
    params.res.mix = static_cast<float>(num("--res-mix"));
    params.res.root = static_cast<float>(num("--res-root"));
    params.res.chord = static_cast<int>(num("--res-chord"));
    params.res.decay = static_cast<float>(num("--res-decay"));
    params.res.color = static_cast<float>(num("--res-color"));
    params.filter = {static_cast<float>(num("--cutoff")), static_cast<float>(num("--reso")), static_cast<float>(num("--morph"))};
    auto f = [&](const char* key) { return static_cast<float>(num(key)); };
    params.color = {f("--drive"), f("--char"), f("--bits"), f("--crush-rate"), f("--squash"), f("--tilt"), f("--color-out"), f("--color-mix")};
    params.eq = {f("--eq-low"), f("--eq-mid"), f("--eq-mid-freq"), f("--eq-high")};
    mensis::LoopTrack track;
    track.prepare(rate);
    juce::AudioBuffer<float> out(2, static_cast<int>(seconds * rate));
    out.clear();
    constexpr int kBlock = 512; // render in blocks, like a host does
    for (int pos = 0; pos < out.getNumSamples(); pos += kBlock)
        track.render(*sample, params, pos * bpm / (60.0 * rate), bpm, out, pos, std::min(kBlock, out.getNumSamples() - pos));

    if (const auto send = static_cast<float>(num("--send")); send > 0.0f) { // the track into the shared Space
        mensis::Space space;
        space.prepare(rate);
        const mensis::SpaceParams sp{num("--space-time"), f("--space-feedback"), f("--space-blend"), f("--space-size"),
                                     f("--space-tone"), 1.0f, false};
        juce::AudioBuffer<float> wet(out);
        wet.applyGain(send);
        for (int pos = 0; pos < wet.getNumSamples(); pos += kBlock)
            space.process(wet.getWritePointer(0, pos), wet.getWritePointer(1, pos), std::min(kBlock, wet.getNumSamples() - pos), sp, bpm);
        for (int ch = 0; ch < 2; ++ch) out.addFrom(ch, 0, wet, ch, 0, out.getNumSamples());
    }

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
