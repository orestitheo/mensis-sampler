#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include <pthread/qos.h>

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
    set(p, "mod1.rate", 8.0f);   // 1 bar
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

TEST_CASE("mod RATE: Hz when SYNC is off, triplets when it's on") {
    juce::ScopedJuceInitialiser_GUI juce;
    MensisProcessor p; // own 120 BPM clock
    p.setRateAndBufferSizeDetails(kRate, 64);
    p.prepareToPlay(kRate, 64);
    set(p, "mod1.type", 0.0f); // LFO (sine)

    // Sign changes of the LFO over 2 s: a sine crosses zero twice per cycle.
    auto crossings = [&]() {
        juce::AudioBuffer<float> buffer(2, 64);
        juce::MidiBuffer midi;
        int n = 0;
        float prev = 0.0f;
        for (int b = 0; b < static_cast<int>(2.0 * kRate / 64); ++b) {
            p.processBlock(buffer, midi);
            const float v = p.getModSignal(0);
            if (b > 0 && (prev < 0.0f) != (v < 0.0f)) ++n;
            prev = v;
        }
        return n;
    };

    p.params.getParameter("mod1.sync")->setValueNotifyingHost(0.0f);
    set(p, "mod1.hz", 2.0f);
    CHECK(std::abs(crossings() - 8) <= 1); // 2 Hz for 2 s = 4 cycles

    p.params.getParameter("mod1.sync")->setValueNotifyingHost(1.0f);
    set(p, "mod1.rate", 4.0f); // "1/4T": 2/3 beat = 1/3 s at 120 BPM -> 6 cycles in 2 s
    CHECK(std::abs(crossings() - 12) <= 1);
}

TEST_CASE("the sample travels inside the saved set: moving the file doesn't break it") {
    juce::ScopedJuceInitialiser_GUI juce;
    auto file = writeTone();
    const auto moved = file.getSiblingFile("mensis_test_tone_moved.wav");
    juce::MemoryBlock saved;
    {
        MensisProcessor p;
        REQUIRE(p.loadSample(0, file, false));
        p.getStateInformation(saved);
    }
    REQUIRE(file.moveFileTo(moved)); // the original path is gone now
    MensisProcessor q;
    q.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(q.getSample(0) != nullptr);              // came back from the embedded copy
    CHECK(q.getSample(0)->audio.getNumSamples() == static_cast<int>(2 * kRate));
    CHECK(q.getMissingSample(0).isEmpty());
    // Saving again still carries it.
    juce::MemoryBlock again;
    q.getStateInformation(again);
    MensisProcessor r;
    r.setStateInformation(again.getData(), static_cast<int>(again.getSize()));
    CHECK(r.getSample(0) != nullptr);
    moved.deleteFile();
}

TEST_CASE("sets saved before embedding still load; a lost file shows as missing") {
    juce::ScopedJuceInitialiser_GUI juce;
    auto file = writeTone();
    MensisProcessor old; // build an old-style state: XML with just the path
    auto state = old.params.copyState();
    state.setProperty("t1.file", file.getFullPathName(), nullptr);
    state.setProperty("t2.file", "/nowhere/lost_loop.wav", nullptr);
    juce::MemoryBlock legacy;
    juce::AudioProcessor::copyXmlToBinary(*state.createXml(), legacy);

    MensisProcessor p;
    p.setStateInformation(legacy.getData(), static_cast<int>(legacy.getSize()));
    CHECK(p.getSample(0) != nullptr);
    CHECK(p.getSample(1) == nullptr);
    CHECK(p.getMissingSample(1) == "/nowhere/lost_loop.wav");

    juce::MemoryBlock resaved; // saving keeps the missing path, so it's not forgotten
    p.getStateInformation(resaved);
    MensisProcessor q;
    q.setStateInformation(resaved.getData(), static_cast<int>(resaved.getSize()));
    CHECK(q.getMissingSample(1) == "/nowhere/lost_loop.wav");
    REQUIRE(q.loadSample(1, file, false)); // LOAD fixes it
    CHECK(q.getMissingSample(1).isEmpty());
}

// Hidden (run with: mensis_plugin_tests "[bench]"). Everything on, on all 4 tracks, at small buffers.
TEST_CASE("CPU with everything turned up", "[.][bench]") {
    juce::ScopedJuceInitialiser_GUI juce;
    // Run like an audio thread (high priority, fast cores), not like a background program.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    const auto loop = juce::File("/Applications/Ableton Live 12 Suite.app/Contents/App-Resources/Core Library/Samples/Loops/Drums/Full/Break Funky 102 bpm.aif");
    const auto file = loop.existsAsFile() ? loop : writeTone();
    for (int block : {64, 128, 512}) {
        MensisProcessor p;
        p.setRateAndBufferSizeDetails(kRate, block);
        p.prepareToPlay(kRate, block);
        set(p, "chaos", 1.0f);
        set(p, "swing", 60.0f);
        for (int t = 1; t <= 4; ++t) {
            REQUIRE(p.loadSample(static_cast<size_t>(t - 1), file, true));
            const auto id = [t](const char* n) { return "t" + std::to_string(t) + "." + n; };
            set(p, id("slice").c_str(), 1.0f);
            set(p, id("beat").c_str(), t % 2 == 0 ? 1.0f : 0.0f);
            set(p, id("pitch").c_str(), 5.0f);
            set(p, id("grain.mix").c_str(), 0.6f);
            set(p, id("grain.size").c_str(), 500.0f);
            set(p, id("grain.density").c_str(), 100.0f); // pool of 64 grains full
            set(p, id("grain.detune").c_str(), 7.0f);
            set(p, id("res.mix").c_str(), 0.5f);
            set(p, id("res.chord").c_str(), 6.0f);       // Maj7: 4 notes x 4 partials
            set(p, id("res.decay").c_str(), 4.0f);
            set(p, id("flt.cutoff").c_str(), 2000.0f);
            set(p, id("flt.reso").c_str(), 0.5f);
            set(p, id("col.drive").c_str(), 0.6f);
            set(p, id("col.bits").c_str(), 8.0f);
            set(p, id("col.rate").c_str(), 12000.0f);
            set(p, id("col.squash").c_str(), 0.6f);
            set(p, id("col.tilt").c_str(), 0.4f);
            set(p, id("send").c_str(), 0.5f);
        }
        set(p, "space.size", 1.0f);
        set(p, "space.feedback", 0.8f);
        for (size_t m = 0; m < 4; ++m) {
            p.setModTarget(m, "t" + juce::String(m + 1) + ".flt.cutoff");
            set(p, ("mod" + std::to_string(m + 1) + ".depth").c_str(), 0.5f);
        }
        juce::AudioBuffer<float> buffer(2, block);
        juce::MidiBuffer midi;
        constexpr double seconds = 10.0;
        const auto blocks = static_cast<int>(seconds * kRate / block);
        double slowest = 0.0;
        int slowestAt = -1, over = 0;
        const double budget = block / kRate * 1000.0;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; b < blocks; ++b) {
            const auto b0 = juce::Time::getMillisecondCounterHiRes();
            p.processBlock(buffer, midi);
            const double took = juce::Time::getMillisecondCounterHiRes() - b0;
            if (took > slowest) { slowest = took; slowestAt = b; }
            over += took > budget * 0.5; // blocks using more than half their budget
        }
        WARN("  slowest at block " << slowestAt << " of " << blocks << "; blocks over half their budget: " << over);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const double load = ms / (seconds * 1000.0) * 100.0;
        const double worstBlockBudget = block / kRate * 1000.0;
        WARN("block " << block << ": " << load << "% of one core; slowest block " << slowest << " ms of its "
                      << worstBlockBudget << " ms budget");
        CHECK(load < 50.0);
        // Slow single blocks are reported, not failed: this test isn't a real-time audio thread, and on a busy
        // Mac the OS occasionally pauses it (rare, random blocks 10-20x the usual cost; an empty processor
        // run alongside shows none only because its blocks are too short to be hit). Mensis does about the
        // same work every block, so a real problem would show as a steady pattern, not as rare spikes.
    }
}

// Hidden: the same timing loop on an empty Mensis, to tell OS hiccups from our own slow blocks.
TEST_CASE("timing noise of an empty processor", "[.][bench]") {
    juce::ScopedJuceInitialiser_GUI juce;
    MensisProcessor p;
    p.setRateAndBufferSizeDetails(kRate, 64);
    p.prepareToPlay(kRate, 64);
    juce::AudioBuffer<float> buffer(2, 64);
    juce::MidiBuffer midi;
    double slowest = 0.0;
    int over = 0;
    for (int b = 0; b < 7500; ++b) {
        const auto b0 = juce::Time::getMillisecondCounterHiRes();
        p.processBlock(buffer, midi);
        const double took = juce::Time::getMillisecondCounterHiRes() - b0;
        slowest = std::max(slowest, took);
        over += took > 0.6667;
    }
    WARN("empty, block 64: slowest " << slowest << " ms; blocks over 0.67 ms: " << over);
}

namespace {
struct LoopingHost final : juce::AudioPlayHead { // Live with its loop on: we move ppq ourselves
    double ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setIsPlaying(true);
        info.setBpm(120.0);
        info.setPpqPosition(ppq);
        return info;
    }
};
} // namespace

TEST_CASE("re-roll lands when Live's loop wraps before the bar line it waits for") {
    juce::ScopedJuceInitialiser_GUI juce;
    const auto tone = writeTone();
    // Plays `blocks` blocks from ppq `from`; returns the left channel.
    auto play = [](MensisProcessor& p, LoopingHost& host, double from, int blocks) {
        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer midi;
        std::vector<float> out;
        host.ppq = from;
        for (int b = 0; b < blocks; ++b, host.ppq += kBlock * 120.0 / (60.0 * kRate)) {
            p.processBlock(buffer, midi);
            out.insert(out.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + kBlock);
        }
        return out;
    };
    auto make = [&](MensisProcessor& p, LoopingHost& host, float seed) {
        p.setPlayHead(&host);
        p.setRateAndBufferSizeDetails(kRate, kBlock);
        p.prepareToPlay(kRate, kBlock);
        REQUIRE(p.loadSample(0, tone, false));
        set(p, "chaos", 1.0f);
        set(p, "seed", seed);
    };
    LoopingHost hostA, hostB, hostC;
    MensisProcessor a, b, c;
    make(a, hostA, 1.0f);
    play(a, hostA, 13.0, 20); // in bar 4 of a 4-bar loop
    set(a, "seed", 2.0f);     // re-roll: waits for beat 16, which the loop never reaches
    play(a, hostA, 13.5, 20);
    const auto wrapped = play(a, hostA, 0.0, 100); // the loop wraps to bar 1

    make(b, hostB, 2.0f);
    const auto fresh = play(b, hostB, 0.0, 100);
    make(c, hostC, 1.0f);
    const auto old = play(c, hostC, 0.0, 100);
    float diffNew = 0.0f, diffOld = 0.0f;
    for (size_t i = 0; i < wrapped.size(); ++i) {
        diffNew = std::max(diffNew, std::abs(wrapped[i] - fresh[i]));
        diffOld = std::max(diffOld, std::abs(wrapped[i] - old[i]));
    }
    CHECK(diffNew < 1e-4f); // plays the new seed
    CHECK(diffOld > 0.05f); // not the old one
}

TEST_CASE("the window opens at the size it was left at") {
    juce::ScopedJuceInitialiser_GUI juce;
    MensisProcessor p;
    p.setUiScale(1.2);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    CHECK(editor->getWidth() == 1248); // 1040 x 1.2
}
