#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {

const juce::StringArray kBarNames{"1/4", "1/2", "1", "2", "4", "8", "16"};
constexpr std::array<double, 7> kBarValues{0.25, 0.5, 1, 2, 4, 8, 16};
const juce::StringArray kGridNames{"1/32", "1/16", "1/8", "1/4"};
constexpr std::array<double, 4> kGridBeats{0.125, 0.25, 0.5, 1.0};
const juce::StringArray kFeelNames{"Grid", "Loose", "Free"};
const juce::StringArray kDelayNames{"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/4D", "1/2", "1 bar"};
constexpr std::array<double, 12> kDelayBeats{0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75, 1.0, 1.5, 2.0, 4.0};

// Param IDs are stable: never rename once used in Live (automation and presets would break).
juce::String paramId(size_t track, const char* name) { return "t" + juce::String(track + 1) + "." + name; }

juce::AudioProcessorValueTreeState::ParameterLayout makeLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // One track's params per group, in order, so Push banks of 8 line up with tracks.
    for (size_t t = 0; t < MensisProcessor::kNumTracks; ++t) {
        auto id = [t](const char* name) { return juce::ParameterID{paramId(t, name), 1}; };
        auto label = [t](const char* name) { return "T" + juce::String(t + 1) + " " + name; };
        layout.add(std::make_unique<juce::AudioParameterBool>(id("on"), label("On"), true),
                   std::make_unique<juce::AudioParameterFloat>(id("start"), label("Start"), 0.0f, 1.0f, 0.0f),
                   std::make_unique<juce::AudioParameterFloat>(id("length"), label("Length"), 0.01f, 1.0f, 1.0f),
                   std::make_unique<juce::AudioParameterChoice>(id("bars"), label("Bars"), kBarNames, 2),
                   std::make_unique<juce::AudioParameterFloat>(id("pitch"), label("Pitch"),
                       juce::NormalisableRange<float>(-24.0f, 24.0f, 1.0f), 0.0f,
                       juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
                           const int st = juce::roundToInt(v);
                           return (st > 0 ? "+" : "") + juce::String(st) + " st";
                       })),
                   std::make_unique<juce::AudioParameterBool>(id("reverse"), label("Reverse"), false),
                   std::make_unique<juce::AudioParameterBool>(id("warp"), label("Warp"), true),
                   std::make_unique<juce::AudioParameterBool>(id("slice"), label("Slice"), false),
                   std::make_unique<juce::AudioParameterFloat>(id("level"), label("Level"),
                       juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f), -6.0f),
                   std::make_unique<juce::AudioParameterFloat>(id("pan"), label("Pan"), -1.0f, 1.0f, 0.0f),
                   std::make_unique<juce::AudioParameterFloat>(id("chaos"), label("Chaos"), 0.0f, 1.0f, 1.0f));
    }
    // Global Chaos: after the tracks, so each track stays one Push bank of 8.
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"chaos", 1}, "Chaos", 0.0f, 1.0f, 0.0f),
               std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"chaos.grid", 1}, "Chaos Grid", kGridNames, 1),
               std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"chaos.feel", 1}, "Chaos Feel", kFeelNames, 0),
               std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"seed", 1}, "Seed", 0, 9999, 1));

    // Grain clouds: one group of 8 per track, after the globals, so each is one Push bank.
    auto shown = [](auto fn) { return juce::AudioParameterFloatAttributes().withStringFromValueFunction(fn); };
    auto percent = shown([](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; });
    for (size_t t = 0; t < MensisProcessor::kNumTracks; ++t) {
        auto id = [t](const char* name) { return juce::ParameterID{paramId(t, name), 1}; };
        auto label = [t](const char* name) { return "T" + juce::String(t + 1) + " Grain " + name; };
        auto range = [](float lo, float hi, float centre) {
            juce::NormalisableRange<float> r(lo, hi);
            r.setSkewForCentre(centre); // more knob travel for the small values
            return r;
        };
        layout.add(std::make_unique<juce::AudioParameterFloat>(id("grain.mix"), label("Mix"), juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f, percent),
                   std::make_unique<juce::AudioParameterFloat>(id("grain.size"), label("Size"), range(10.0f, 500.0f, 80.0f), 80.0f,
                       shown([](float v, int) { return juce::String(juce::roundToInt(v)) + " ms"; })),
                   std::make_unique<juce::AudioParameterFloat>(id("grain.density"), label("Density"), range(1.0f, 100.0f, 20.0f), 20.0f,
                       shown([](float v, int) { return juce::String(v, v < 10.0f ? 1 : 0) + " /s"; })),
                   std::make_unique<juce::AudioParameterFloat>(id("grain.spray"), label("Spray"), juce::NormalisableRange<float>(0.0f, 1.0f), 0.2f, percent),
                   std::make_unique<juce::AudioParameterFloat>(id("grain.detune"), label("Detune"), juce::NormalisableRange<float>(0.0f, 12.0f), 0.0f,
                       shown([](float v, int) { return juce::String(v, 1) + " st"; })),
                   std::make_unique<juce::AudioParameterFloat>(id("grain.reverse"), label("Reverse"), juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f, percent),
                   std::make_unique<juce::AudioParameterFloat>(id("grain.width"), label("Width"), juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f, percent),
                   std::make_unique<juce::AudioParameterBool>(id("grain.freeze"), label("Freeze"), false));
    }
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"master", 1}, "Volume",
                   juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f, 2.5f), 0.0f, // skew: fine control near 0 dB
                   juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
                       return v <= -60.0f ? juce::String("-inf") : juce::String(v, 1) + " dB";
                   })));

    // Resonator + filter: one group of 8 per track.
    juce::StringArray chordNames;
    for (const auto& c : mensis::kChords) chordNames.add(c.name);
    auto text = [](auto fn) { return juce::AudioParameterFloatAttributes().withStringFromValueFunction(fn); };
    auto pct = text([](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; });
    for (size_t t = 0; t < MensisProcessor::kNumTracks; ++t) {
        auto id = [t](const char* name) { return juce::ParameterID{paramId(t, name), 1}; };
        auto label = [t](const char* name) { return "T" + juce::String(t + 1) + " " + name; };
        juce::NormalisableRange<float> decay(0.05f, 10.0f), cutoff(20.0f, 20000.0f);
        decay.setSkewForCentre(1.0f);
        cutoff.setSkewForCentre(1000.0f);
        layout.add(std::make_unique<juce::AudioParameterFloat>(id("res.mix"), label("Res Mix"), juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f, pct),
                   std::make_unique<juce::AudioParameterFloat>(id("res.root"), label("Res Root"),
                       juce::NormalisableRange<float>(24.0f, 96.0f, 1.0f), 48.0f,
                       text([](float v, int) { return juce::MidiMessage::getMidiNoteName(juce::roundToInt(v), true, true, 3); })),
                   std::make_unique<juce::AudioParameterChoice>(id("res.chord"), label("Res Chord"), chordNames, 0),
                   std::make_unique<juce::AudioParameterFloat>(id("res.decay"), label("Res Decay"), decay, 1.0f,
                       text([](float v, int) { return v < 1.0f ? juce::String(juce::roundToInt(v * 1000.0f)) + " ms" : juce::String(v, 1) + " s"; })),
                   std::make_unique<juce::AudioParameterFloat>(id("res.color"), label("Res Color"), juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
                       text([](float v, int) { return v < 0.02f ? juce::String("String") : v > 0.98f ? juce::String("Bell") : juce::String(juce::roundToInt(v * 100.0f)) + "%"; })),
                   std::make_unique<juce::AudioParameterFloat>(id("flt.cutoff"), label("Cutoff"), cutoff, 20000.0f,
                       text([](float v, int) { return v < 1000.0f ? juce::String(juce::roundToInt(v)) + " Hz" : juce::String(v / 1000.0f, 1) + " kHz"; })),
                   std::make_unique<juce::AudioParameterFloat>(id("flt.reso"), label("Reso"), juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f, pct),
                   std::make_unique<juce::AudioParameterFloat>(id("flt.morph"), label("Morph"), juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
                       text([](float v, int) {
                           if (v < 0.02f) return juce::String("LP");
                           if (std::abs(v - 0.5f) < 0.02f) return juce::String("BP");
                           if (v > 0.98f) return juce::String("HP");
                           return juce::String(v < 0.5f ? "LP>BP " : "BP>HP ") + juce::String(juce::roundToInt(std::fmod(v * 2.0f, 1.0f) * 100.0f)) + "%";
                       })));
    }

    // Color: one group of 8 per track.
    for (size_t t = 0; t < MensisProcessor::kNumTracks; ++t) {
        auto id = [t](const char* name) { return juce::ParameterID{paramId(t, name), 1}; };
        auto label = [t](const char* name) { return "T" + juce::String(t + 1) + " " + name; };
        juce::NormalisableRange<float> rate(200.0f, 48000.0f);
        rate.setSkewForCentre(4000.0f);
        layout.add(std::make_unique<juce::AudioParameterFloat>(id("col.drive"), label("Drive"), juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f, pct),
                   std::make_unique<juce::AudioParameterFloat>(id("col.char"), label("Character"), juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
                       text([](float v, int) { return v < 0.02f ? juce::String("Warm") : v > 0.98f ? juce::String("Fuzz") : juce::String(juce::roundToInt(v * 100.0f)) + "%"; })),
                   std::make_unique<juce::AudioParameterFloat>(id("col.bits"), label("Bits"), juce::NormalisableRange<float>(1.0f, 16.0f), 16.0f,
                       text([](float v, int) { return v >= 16.0f ? juce::String("Off") : juce::String(v, 1) + " bit"; })),
                   std::make_unique<juce::AudioParameterFloat>(id("col.rate"), label("Rate"), rate, 48000.0f,
                       text([](float v, int) { return v >= 48000.0f ? juce::String("Off") : v < 1000.0f ? juce::String(juce::roundToInt(v)) + " Hz" : juce::String(v / 1000.0f, 1) + " kHz"; })),
                   std::make_unique<juce::AudioParameterFloat>(id("col.squash"), label("Squash"), juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f, pct),
                   std::make_unique<juce::AudioParameterFloat>(id("col.tilt"), label("Tilt"), juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
                       text([](float v, int) {
                           if (std::abs(v) < 0.02f) return juce::String("Flat");
                           return juce::String(v < 0.0f ? "Dark " : "Bright ") + juce::String(juce::roundToInt(std::abs(v) * 100.0f)) + "%";
                       })),
                   std::make_unique<juce::AudioParameterFloat>(id("col.out"), label("Color Out"), juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f,
                       text([](float v, int) { return juce::String(v, 1) + " dB"; })),
                   std::make_unique<juce::AudioParameterFloat>(id("col.mix"), label("Color Mix"), juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f, pct));
    }

    // Space: one shared delay + reverb, one SEND per track.
    for (size_t t = 0; t < MensisProcessor::kNumTracks; ++t)
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{paramId(t, "send"), 1},
                       "T" + juce::String(t + 1) + " Send", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f, pct));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"space.time", 1}, "Space Time", kDelayNames, 7),
               std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"space.feedback", 1}, "Space Feedback",
                   juce::NormalisableRange<float>(0.0f, 0.95f), 0.4f, pct),
               std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"space.blend", 1}, "Space Blend",
                   juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f,
                   text([](float v, int) { return v < 0.02f ? juce::String("Delay") : v > 0.98f ? juce::String("Reverb") : juce::String(juce::roundToInt(v * 100.0f)) + "% verb"; })),
               std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"space.size", 1}, "Space Size",
                   juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f,
                   text([](float v, int) { return juce::String(0.5 * std::pow(40.0, v), 1) + " s"; })),
               std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"space.tone", 1}, "Space Tone",
                   juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f, pct),
               std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"space.return", 1}, "Space Return",
                   juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f, 2.5f), 0.0f,
                   text([](float v, int) { return v <= -60.0f ? juce::String("-inf") : juce::String(v, 1) + " dB"; })),
               std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"space.freeze", 1}, "Space Freeze", false));
    return layout;
}

} // namespace

MensisProcessor::MensisProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "MensisSampler", makeLayout()) {
    for (size_t t = 0; t < kNumTracks; ++t) {
        auto raw = [&](const char* name) { return params.getRawParameterValue(paramId(t, name)); };
        trackParams[t] = {raw("start"), raw("length"), raw("bars"), raw("reverse"), raw("level"), raw("pan"), raw("warp"), raw("chaos"), raw("pitch"), raw("slice"),
                          raw("grain.mix"), raw("grain.size"), raw("grain.density"), raw("grain.spray"),
                          raw("grain.detune"), raw("grain.reverse"), raw("grain.width"), raw("grain.freeze"), raw("on"),
                          raw("res.mix"), raw("res.root"), raw("res.chord"), raw("res.decay"), raw("res.color"),
                          raw("flt.cutoff"), raw("flt.reso"), raw("flt.morph"),
                          raw("col.drive"), raw("col.char"), raw("col.bits"), raw("col.rate"), raw("col.squash"),
                          raw("col.tilt"), raw("col.out"), raw("col.mix")};
        playPos[t].store(-1.0f);
        grainAnchor[t].store(-1.0f);
    }
    chaosAmount = params.getRawParameterValue("chaos");
    chaosGrid = params.getRawParameterValue("chaos.grid");
    chaosFeel = params.getRawParameterValue("chaos.feel");
    seedParam = params.getRawParameterValue("seed");
    masterDb = params.getRawParameterValue("master");
    for (size_t t = 0; t < kNumTracks; ++t) sendParams[t] = params.getRawParameterValue(paramId(t, "send"));
    spaceTime = params.getRawParameterValue("space.time");
    spaceFeedback = params.getRawParameterValue("space.feedback");
    spaceBlend = params.getRawParameterValue("space.blend");
    spaceSize = params.getRawParameterValue("space.size");
    spaceTone = params.getRawParameterValue("space.tone");
    spaceReturn = params.getRawParameterValue("space.return");
    spaceFreeze = params.getRawParameterValue("space.freeze");
}

void MensisProcessor::prepareToPlay(double sampleRate, int maxBlock) {
    for (auto& track : tracks) track.prepare(sampleRate);
    space.prepare(sampleRate);
    const int scratch = std::max(64, maxBlock); // bigger host blocks are processed in chunks of this size
    trackBuf.setSize(2, scratch);
    sendBuf.setSize(2, scratch);
    for (auto& s : sendGain) s.reset(sampleRate, 0.05);
    masterGain.reset(sampleRate, 0.05); // 50 ms glide: no zipper noise or clicks
    masterGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(masterDb->load(), -60.0f));
}

void MensisProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();

    bool playing = true;
    double ppq = internalPpq, bpm = 120.0;
    if (auto* playHead = getPlayHead())
        if (auto pos = playHead->getPosition()) {
            playing = pos->getIsPlaying();
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
        }
    lastBpm.store(bpm);

    if (!playing)
        for (size_t t = 0; t < kNumTracks; ++t) {
            playPos[t].store(-1.0f, std::memory_order_relaxed);
            publishGrains(t, nullptr, false);
        }
    // Re-roll: a new seed takes over at the next bar line. Steps before it keep the old seed, so an event
    // already playing finishes cleanly.
    const auto wantSeed = static_cast<uint32_t>(seedParam->load());
    if (!seedStarted) {
        seedOld = seedNew = wantSeed;
        seedStarted = true;
    } else if (wantSeed != seedNew) {
        if (ppq >= seedSwitch) seedOld = seedNew;
        seedNew = wantSeed;
        seedSwitch = std::ceil(ppq / 4.0) * 4.0; // shortcut: assumes 4/4, like the loop length
    }
    // MIDI notes held right now tune the resonators (the last 4 pressed win). Applied per block.
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        auto drop = [&](int note) {
            const auto end = std::remove(heldNotes.begin(), heldNotes.begin() + numHeld, note);
            numHeld = static_cast<int>(end - heldNotes.begin());
        };
        if (m.isNoteOn()) {
            drop(m.getNoteNumber());
            if (numHeld == static_cast<int>(heldNotes.size())) drop(heldNotes[0]); // full: forget the oldest
            heldNotes[static_cast<size_t>(numHeld++)] = m.getNoteNumber();
        } else if (m.isNoteOff()) {
            drop(m.getNoteNumber());
        } else if (m.isAllNotesOff() || m.isAllSoundOff()) {
            numHeld = 0;
        }
    }
    std::array<float, 4> midiNotes{};
    const int numMidiNotes = std::min(numHeld, 4);
    for (int i = 0; i < numMidiNotes; ++i)
        midiNotes[static_cast<size_t>(i)] = static_cast<float>(heldNotes[static_cast<size_t>(numHeld - numMidiNotes + i)]);

    mensis::ChaosParams chaos;
    chaos.gridBeats = kGridBeats[static_cast<size_t>(chaosGrid->load())];
    const auto feel = static_cast<int>(chaosFeel->load());
    chaos.seed = seedOld;
    chaos.nextSeed = seedNew;
    chaos.switchBeats = seedSwitch;

    // Each track's settings for this block.
    std::array<mensis::LoopParams, kNumTracks> lps;
    for (size_t t = 0; t < kNumTracks; ++t) {
        const auto& tp = trackParams[t];
        auto& lp = lps[t];
        lp = {tp.start->load(), tp.length->load(), kBarValues[static_cast<size_t>(tp.bars->load())],
              tp.reverse->load() > 0.5f, tp.level->load(), tp.pan->load(), tp.warp->load() > 0.5f, chaos};
        lp.chaos.amount = chaosAmount->load() * tp.chaos->load();
        lp.chaos.looseness = feel == 0 ? 0.0f : feel == 1 ? lp.chaos.amount : 1.0f; // Loose: drifts more as Chaos rises
        lp.chaos.stream = static_cast<uint32_t>(t);
        lp.pitch = tp.pitch->load();
        lp.slice = tp.slice->load() > 0.5f;
        lp.on = tp.on->load() > 0.5f;
        lp.res = {tp.resMix->load(), tp.resRoot->load(), static_cast<int>(tp.resChord->load()), tp.resDecay->load(),
                  tp.resColor->load(), midiNotes, numMidiNotes};
        lp.filter = {tp.fltCutoff->load(), tp.fltReso->load(), tp.fltMorph->load()};
        const float crushRate = tp.colRate->load();
        lp.color = {tp.colDrive->load(), tp.colChar->load(), tp.colBits->load(),
                    crushRate >= 48000.0f ? 1e9f : crushRate, // knob at the top = off, at any host rate
                    tp.colSquash->load(), tp.colTilt->load(), tp.colOut->load(), tp.colMix->load()};
        lp.grains = {tp.grainMix->load(), tp.grainSize->load(), tp.grainDensity->load(), tp.grainSpray->load(),
                     tp.grainDetune->load(), tp.grainReverse->load(), tp.grainWidth->load(),
                     tp.grainFreeze->load() > 0.5f, lp.chaos.amount, seedNew, static_cast<uint32_t>(t)};
        sendGain[t].setTargetValue(sendParams[t]->load());
    }
    const mensis::SpaceParams sp{kDelayBeats[static_cast<size_t>(spaceTime->load())], spaceFeedback->load(),
                                 spaceBlend->load(), spaceSize->load(), spaceTone->load(),
                                 juce::Decibels::decibelsToGain(spaceReturn->load(), -60.0f), spaceFreeze->load() > 0.5f};

    if (buffer.getNumChannels() >= 2) {
        // In chunks that fit the scratch buffers. Each track renders into trackBuf, goes into the mix and,
        // through its SEND, into the shared Space. The Space keeps running when Live stops, so tails ring out.
        const int chunk = trackBuf.getNumSamples();
        for (int off = 0; off < numSamples; off += chunk) {
            const int n = std::min(chunk, numSamples - off);
            const double chunkPpq = ppq + off * bpm / (60.0 * getSampleRate());
            sendBuf.clear(0, n);
            for (size_t t = 0; t < kNumTracks && playing; ++t) {
                const auto* sample = liveSamples[t].load(std::memory_order_acquire);
                if (sample == nullptr) continue;
                trackBuf.clear(0, n);
                tracks[t].render(*sample, lps[t], chunkPpq, bpm, trackBuf, 0, n);
                for (int ch = 0; ch < 2; ++ch) buffer.addFrom(ch, off, trackBuf, ch, 0, n);
                for (int i = 0; i < n; ++i) {
                    const float g = sendGain[t].getNextValue();
                    sendBuf.addSample(0, i, trackBuf.getSample(0, i) * g);
                    sendBuf.addSample(1, i, trackBuf.getSample(1, i) * g);
                }
            }
            space.process(sendBuf.getWritePointer(0), sendBuf.getWritePointer(1), n, sp, bpm);
            for (int ch = 0; ch < 2; ++ch) buffer.addFrom(ch, off, sendBuf, ch, 0, n);
        }
        for (size_t t = 0; t < kNumTracks && playing; ++t) {
            const auto* sample = liveSamples[t].load(std::memory_order_acquire);
            playPos[t].store(sample != nullptr ? tracks[t].position() : -1.0f, std::memory_order_relaxed);
            publishGrains(t, sample, sample != nullptr && lps[t].grains.mix > 0.0f);
        }
        masterGain.setTargetValue(juce::Decibels::decibelsToGain(masterDb->load(), -60.0f));
        masterGain.applyGain(buffer, numSamples);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) // safety net: never send more than 0 dBFS to Live
            juce::FloatVectorOperations::clip(buffer.getWritePointer(ch), buffer.getReadPointer(ch), -1.0f, 1.0f, numSamples);
    }
    internalPpq = ppq + numSamples * bpm / (60.0 * getSampleRate());
    blocksDone.fetch_add(1, std::memory_order_release);
}

// Copies the grain cloud's state into atomics the UI reads (audio thread; no locks, no allocation).
void MensisProcessor::publishGrains(size_t track, const mensis::Sample* sample, bool on) {
    const auto& cloud = tracks[track].grains();
    const int n = sample != nullptr ? sample->audio.getNumSamples() : 1;
    for (size_t i = 0; i < kMaxGrains; ++i) {
        const auto v = on ? cloud.view(i, n) : mensis::GrainCloud::View{};
        auto& d = grainDots[track][i];
        d.x.store(v.x, std::memory_order_relaxed);
        d.env.store(v.backwards ? -v.env : v.env, std::memory_order_relaxed);
        d.pan.store(v.pan, std::memory_order_relaxed);
    }
    grainAnchor[track].store(on ? cloud.anchor(n) : -1.0f, std::memory_order_relaxed);
}

bool MensisProcessor::loadSample(size_t track, const juce::File& file, bool fresh) {
    auto sample = mensis::loadSample(file);
    if (sample == nullptr) return false;

    if (fresh) {
        auto* slice = params.getParameter(paramId(track, "slice"));
        slice->setValueNotifyingHost(sample->drumLike ? 1.0f : 0.0f);
        const double bars = sample->lengthSec() * lastBpm.load() / 240.0; // 4 beats per bar
        size_t best = 0;
        for (size_t i = 1; i < kBarValues.size(); ++i)
            if (std::abs(std::log2(kBarValues[i] / bars)) < std::abs(std::log2(kBarValues[best] / bars))) best = i;
        auto* p = params.getParameter(paramId(track, "bars"));
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(best)));
    }

    swapIn(track, std::move(sample));
    return true;
}

void MensisProcessor::clearSample(size_t track) { swapIn(track, nullptr); }

void MensisProcessor::swapIn(size_t track, std::unique_ptr<mensis::Sample> sample) {
    // The old sample may still be in use by the block that is running right now,
    // so keep it until the audio thread has finished at least one more full block.
    liveSamples[track].store(sample.get(), std::memory_order_release);
    if (ownedSamples[track] != nullptr)
        retired.push_back({std::move(ownedSamples[track]), blocksDone.load(std::memory_order_acquire)});
    ownedSamples[track] = std::move(sample);

    const auto done = blocksDone.load(std::memory_order_acquire);
    std::erase_if(retired, [done](const Retired& r) { return done > r.retiredAtBlock + 1; });
}

double MensisProcessor::getBars(size_t track) const {
    return kBarValues[static_cast<size_t>(trackParams[track].bars->load())];
}

juce::File MensisProcessor::getSampleFile(size_t track) const {
    return ownedSamples[track] != nullptr ? ownedSamples[track]->file : juce::File{};
}

void MensisProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = params.copyState();
    // shortcut: saves the file path, not the audio; if the file moves, the track loads empty
    for (size_t t = 0; t < kNumTracks; ++t)
        state.setProperty(paramId(t, "file"), getSampleFile(t).getFullPathName(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void MensisProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(params.state.getType())) return;
    auto state = juce::ValueTree::fromXml(*xml);
    params.replaceState(state);
    for (size_t t = 0; t < kNumTracks; ++t) {
        const auto path = state.getProperty(paramId(t, "file")).toString();
        if (path.isNotEmpty()) loadSample(t, juce::File(path), false);
        else clearSample(t);
    }
}

juce::AudioProcessorEditor* MensisProcessor::createEditor() { return new MensisEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MensisProcessor(); }
