// demea-cli: render audio offline (no DAW) so it can be checked and plotted.
// For now `render` writes silence; each new core/ feature adds a flag or subcommand here.
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include "core/RenderStats.h"

static int usage() {
    std::cerr << "usage: demea-cli render [--seconds N] [--rate HZ] --out file.wav\n";
    return 1;
}

int main(int argc, char* argv[]) {
    if (argc < 2 || std::string(argv[1]) != "render") return usage();

    double seconds = 1.0, sampleRate = 48000.0;
    std::string outPath;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string key = argv[i], value = argv[i + 1];
        if (key == "--seconds") seconds = std::stod(value);
        else if (key == "--rate") sampleRate = std::stod(value);
        else if (key == "--out") outPath = value;
        else return usage();
    }
    if (outPath.empty() || seconds <= 0.0 || sampleRate <= 0.0) return usage();

    juce::AudioBuffer<float> buffer(2, static_cast<int>(seconds * sampleRate));
    buffer.clear();

    const juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile(outPath);
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = outFile.createOutputStream();
    if (stream == nullptr) { std::cerr << "can't open " << outPath << "\n"; return 1; }

    juce::WavAudioFormat wav;
    // 32-bit float so overs (> 0 dBFS) survive in the file and show up in checks.
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
        .withSampleRate(sampleRate).withNumChannels(2).withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (writer == nullptr || !writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples())) {
        std::cerr << "failed writing " << outPath << "\n";
        return 1;
    }

    bool finite = true; float peak = 0.0f; double dc = 0.0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        auto s = demea::measure({buffer.getReadPointer(ch), static_cast<size_t>(buffer.getNumSamples())});
        finite = finite && s.finite;
        peak = std::max(peak, s.peak);
        dc = std::max(dc, std::abs(s.dcOffset));
    }
    std::cout << "{\"out\": \"" << outPath << "\", \"finite\": " << (finite ? "true" : "false")
              << ", \"peak\": " << peak << ", \"dc\": " << dc << "}\n";
    return finite ? 0 : 2;
}
