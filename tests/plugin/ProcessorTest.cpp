#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

// A 2 s, 440 Hz tone written to a temp WAV, so the processor loads it like a real sample.
juce::File writeTone() {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mensis_test_tone.wav");
    file.deleteFile();
    juce::AudioBuffer<float> tone(1, static_cast<int>(2 * kRate));
    for (int i = 0; i < tone.getNumSamples(); ++i)
        tone.setSample(0, i, 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * i / static_cast<float>(kRate)));
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(kRate).withNumChannels(1).withBitsPerSample(24));
    writer->writeFromAudioSampleBuffer(tone, 0, tone.getNumSamples());
    return file;
}

void set(MensisProcessor& p, const char* id, float value) {
    auto* param = p.params.getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

// Zero crossings per second in each quarter-second window of the left channel.
std::vector<double> pitchTrack(MensisProcessor& p, double seconds) {
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    std::vector<float> out;
    for (int done = 0; done < static_cast<int>(seconds * kRate); done += kBlock) {
        p.processBlock(buffer, midi);
        out.insert(out.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + kBlock);
    }
    std::vector<double> hz;
    const size_t win = static_cast<size_t>(kRate / 4);
    for (size_t w = 0; w + win <= out.size(); w += win) {
        int crossings = 0;
        for (size_t i = w + 1; i < w + win; ++i) crossings += (out[i - 1] < 0.0f) != (out[i] < 0.0f);
        hz.push_back(crossings / 2.0 * 4.0);
    }
    return hz;
}

} // namespace

TEST_CASE("a modulator mapped to PITCH moves the pitch") {
    juce::ScopedJuceInitialiser_GUI juce;
    MensisProcessor p; // no host: plays on its own 120 BPM clock
    p.setRateAndBufferSizeDetails(kRate, kBlock); // what a host does before playing
    p.prepareToPlay(kRate, kBlock);
    REQUIRE(p.loadSample(0, writeTone(), false));
    set(p, "t1.bars", 2.0f);  // index 2 = "1" bar = 2 s at 120 BPM: plays at its own speed
    set(p, "t1.level", 0.0f);

    const auto steady = pitchTrack(p, 2.0);
    for (double hz : steady) CHECK(std::abs(hz - 440.0) < 30.0);

    set(p, "mod1.type", 0.0f);   // LFO
    set(p, "mod1.rate", 4.0f);   // 1 bar
    set(p, "mod1.depth", 0.25f); // +-25% of the PITCH range = +-12 st
    p.setModTarget(0, "t1.pitch");
    REQUIRE(p.getModTarget(0) == "t1.pitch");
    const auto moving = pitchTrack(p, 2.0);
    const auto [lo, hi] = std::minmax_element(moving.begin(), moving.end());
    INFO("lowest " << *lo << " Hz, highest " << *hi << " Hz");
    CHECK(*hi > 600.0); // up toward 880 Hz
    CHECK(*lo < 330.0); // down toward 220 Hz
}

TEST_CASE("a recalled scene takes over exactly on the next bar, then the knobs show it") {
    juce::ScopedJuceInitialiser_GUI juce;
    MensisProcessor p; // own 120 BPM clock: one bar = 2 s = 96000 samples
    p.setRateAndBufferSizeDetails(kRate, kBlock);
    p.prepareToPlay(kRate, kBlock);
    REQUIRE(p.loadSample(0, writeTone(), false));
    set(p, "t1.bars", 2.0f);

    set(p, "t1.level", -6.0f);
    p.saveScene(0);             // loud
    set(p, "t1.level", -30.0f); // now quiet
    REQUIRE(p.hasScene(0));

    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    std::vector<float> out;
    auto run = [&](int blocks) {
        for (int b = 0; b < blocks; ++b) {
            p.processBlock(buffer, midi);
            out.insert(out.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + kBlock);
        }
    };
    run(10);              // a little into bar 1
    p.recallScene(0);     // should wait for the bar line at 96000
    CHECK(p.getPendingScene() == 0);
    run(250);             // past 96000
    auto peak = [&](size_t a, size_t b) {
        float m = 0.0f;
        for (size_t i = a; i < b; ++i) m = std::max(m, std::abs(out[i]));
        return m;
    };
    CHECK(peak(90000, 95990) < 0.03f);  // still quiet right up to the bar
    CHECK(peak(98500, 100000) > 0.2f);  // loud after it (level glides over 50 ms)

    p.updateScenes(); // what the timer does
    auto* level = p.params.getParameter("t1.level");
    CHECK(std::abs(level->convertFrom0to1(level->getValue()) - (-6.0f)) < 0.1f);
    CHECK(p.getCurrentScene() == 0);
    CHECK(p.getPendingScene() == -1);

    set(p, "master", -12.0f); // volume is not part of scenes
    p.saveScene(1);
    set(p, "master", 0.0f);
    p.recallScene(1);
    run(400);
    p.updateScenes();
    auto* master = p.params.getParameter("master");
    CHECK(std::abs(master->convertFrom0to1(master->getValue())) < 0.01f); // still 0 dB, not the scene's -12
}

TEST_CASE("scenes are saved with the set") {
    juce::ScopedJuceInitialiser_GUI juce;
    juce::MemoryBlock saved;
    {
        MensisProcessor p;
        set(p, "chaos", 0.7f);
        p.saveScene(3);
        p.getStateInformation(saved);
    }
    MensisProcessor q;
    q.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(q.hasScene(3));
    CHECK_FALSE(q.hasScene(0));
    set(q, "chaos", 0.0f);
    q.setRateAndBufferSizeDetails(kRate, kBlock);
    q.prepareToPlay(kRate, kBlock);
    q.recallScene(3);
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    q.processBlock(buffer, midi); // own clock starts on a bar line: takes over at once
    q.updateScenes();
    auto* chaos = q.params.getParameter("chaos");
    CHECK(std::abs(chaos->convertFrom0to1(chaos->getValue()) - 0.7f) < 1e-4f);
}
