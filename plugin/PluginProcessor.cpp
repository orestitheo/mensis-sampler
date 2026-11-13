#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "core/engine/Macros.h"

namespace {

const juce::StringArray kBarNames{"1/4", "1/2", "1", "2", "4", "8", "16"};
constexpr std::array<double, 7> kBarValues{0.25, 0.5, 1, 2, 4, 8, 16};
const juce::StringArray kGridNames{"1/32", "1/16", "1/8", "1/4"};
constexpr std::array<double, 4> kGridBeats{0.125, 0.25, 0.5, 1.0};
const juce::StringArray kFeelNames{"Grid", "Loose", "Free"};
const juce::StringArray kDelayNames{"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/4D", "1/2", "1 bar"};
const juce::StringArray kModTypeNames{"LFO", "Random", "Env"};
const juce::StringArray kModRateNames{"1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2T", "1/2",
                                     "1 bar", "2 bars", "4 bars", "8 bars"};
constexpr std::array<double, 12> kModRateBeats{1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 4.0 / 3.0, 2.0,
                                               4.0, 8.0, 16.0, 32.0};
constexpr std::array<double, 12> kDelayBeats{0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75, 1.0, 1.5, 2.0, 4.0};

// Param IDs are stable: never rename once used in Live (automation and presets would break).
juce::String paramId(size_t track, const char* name) { return "t" + juce::String(track + 1) + "." + name; }

// How knobs read: 0..1 values as percent, levels in dB, pan as L/C/R. Typing works too ("50", "50%", "L 30").
juce::AudioParameterFloatAttributes percentText() {
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; })
        .withValueFromStringFunction([](const juce::String& s) { return s.retainCharacters("-0123456789.").getFloatValue() / 100.0f; });
}
juce::AudioParameterFloatAttributes dbText() {
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float v, int) { return v <= -60.0f ? juce::String("-inf") : juce::String(v, 1) + " dB"; })
        .withValueFromStringFunction([](const juce::String& s) { return s.containsIgnoreCase("inf") ? -60.0f : s.getFloatValue(); });
}
juce::AudioParameterFloatAttributes panText() {
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float v, int) {
            const int amount = juce::roundToInt(std::abs(v) * 100.0f);
            return amount == 0 ? juce::String("C") : juce::String(v < 0.0f ? "L " : "R ") + juce::String(amount);
        })
        .withValueFromStringFunction([](const juce::String& s) {
            const float amount = s.retainCharacters("0123456789.").getFloatValue() / 100.0f;
            return s.trim().startsWithIgnoreCase("L") || s.trim().startsWith("-") ? -amount : amount;
        });
}

juce::AudioProcessorValueTreeState::ParameterLayout makeLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // One track's params per group, in order, so Push banks of 8 line up with tracks.
    for (size_t t = 0; t < MensisProcessor::kNumTracks; ++t) {
        auto id = [t](const char* name) { return juce::ParameterID{paramId(t, name), 1}; };
        auto label = [t](const char* name) { return "T" + juce::String(t + 1) + " " + name; };
        layout.add(std::make_unique<juce::AudioParameterBool>(id("on"), label("On"), true),
                   std::make_unique<juce::AudioParameterFloat>(id("start"), label("Start"), juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f, percentText()),
                   std::make_unique<juce::AudioParameterFloat>(id("length"), label("Length"), juce::NormalisableRange<float>(0.01f, 1.0f), 1.0f, percentText()),
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
                       juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f), -6.0f, dbText()),
                   std::make_unique<juce::AudioParameterFloat>(id("pan"), label("Pan"), juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f, panText()),
                   std::make_unique<juce::AudioParameterFloat>(id("chaos"), label("Chaos"), juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f, percentText()));
    }
    // Global Chaos: after the tracks, so each track stays one Push bank of 8.
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"chaos", 1}, "Chaos", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f, percentText()),
               std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"chaos.grid", 1}, "Chaos Grid", kGridNames, 1),
               std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"chaos.feel", 1}, "Chaos Feel", kFeelNames, 0),
               std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"seed", 1}, "Seed", 0, 9999, 1));

    // Grain clouds: one group of 8 per track, after the globals, so each is one Push bank.
    auto shown = [](auto fn) { return juce::AudioParameterFloatAttributes().withStringFromValueFunction(fn); };
    const auto percent = percentText();
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
                   juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f, 2.5f), 0.0f, dbText())); // skew: fine control near 0 dB

    // Resonator + filter: one group of 8 per track.
    juce::StringArray chordNames;
    for (const auto& c : mensis::kChords) chordNames.add(c.name);
    auto text = [](auto fn) { return juce::AudioParameterFloatAttributes().withStringFromValueFunction(fn); };
    const auto pct = percentText();
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
                   juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f, 2.5f), 0.0f, dbText()),
               std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"space.freeze", 1}, "Space Freeze", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"swing", 1}, "Swing",
                   juce::NormalisableRange<float>(50.0f, 75.0f), 50.0f,
                   text([](float v, int) { return juce::String(juce::roundToInt(v)) + "%"; })));
    // Modulators (4 global slots) and the 4 fixed macros.
    for (size_t m = 1; m <= MensisProcessor::kModSlots; ++m) {
        auto id = [m](const char* name) { return juce::ParameterID{"mod" + juce::String(m) + "." + name, 1}; };
        auto label = [m](const char* name) { return "Mod " + juce::String(m) + " " + name; };
        layout.add(std::make_unique<juce::AudioParameterChoice>(id("type"), label("Type"), kModTypeNames, 0),
                   std::make_unique<juce::AudioParameterChoice>(id("rate"), label("Rate"), kModRateNames, 8),
                   std::make_unique<juce::AudioParameterFloat>(id("shape"), label("Shape"), juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f, pct),
                   std::make_unique<juce::AudioParameterFloat>(id("depth"), label("Depth"), juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
                       text([](float v, int) { return (v > 0.005f ? "+" : "") + juce::String(juce::roundToInt(v * 100.0f)) + "%"; })));
        juce::NormalisableRange<float> hz(0.02f, 20.0f);
        hz.setSkewForCentre(1.0f);
        layout.add(std::make_unique<juce::AudioParameterBool>(id("sync"), label("Sync"), true),
                   std::make_unique<juce::AudioParameterFloat>(id("hz"), label("Hz"), hz, 1.0f,
                       text([](float v, int) { return juce::String(v, v < 1.0f ? 2 : 1) + " Hz"; })));
    }
    for (const auto& [mid, name] : {std::pair{"macro.destroy", "Destroy"}, std::pair{"macro.drift", "Drift"},
                                     std::pair{"macro.ring", "Ring"}, std::pair{"macro.space", "Space"}})
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{mid, 1}, name,
                                                               juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f, pct));

    for (size_t t = 0; t < MensisProcessor::kNumTracks; ++t) // BEAT mode Chaos for sliced tracks
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{paramId(t, "beat"), 1},
                                                              "T" + juce::String(t + 1) + " Beat", false));
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
                          raw("col.tilt"), raw("col.out"), raw("col.mix"), raw("beat")};
        playPos[t].store(-1.0f);
        grainAnchor[t].store(-1.0f);
    }
    chaosAmount = params.getRawParameterValue("chaos");
    chaosGrid = params.getRawParameterValue("chaos.grid");
    chaosFeel = params.getRawParameterValue("chaos.feel");
    seedParam = params.getRawParameterValue("seed");
    masterDb = params.getRawParameterValue("master");
    swingParam = params.getRawParameterValue("swing");
    for (size_t m = 0; m < kModSlots; ++m) {
        auto raw = [&](const char* name) { return params.getRawParameterValue("mod" + juce::String(m + 1) + "." + name); };
        modParams[m] = {raw("type"), raw("rate"), raw("shape"), raw("depth"), raw("sync"), raw("hz")};
        modTargetIndex[m].store(-1);
    }
    macroParams = {params.getRawParameterValue("macro.destroy"), params.getRawParameterValue("macro.drift"),
                   params.getRawParameterValue("macro.ring"), params.getRawParameterValue("macro.space")};
    // Every knob, for modulation targets: the param, its raw value and which track it belongs to (-1 = global).
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p)) {
            allParams.push_back(ranged);
            allRaw.push_back(params.getRawParameterValue(ranged->getParameterID()));
            rawIndex[allRaw.back()] = static_cast<int>(allRaw.size() - 1);
            const auto pid = ranged->getParameterID();
            paramTrack.push_back(pid.startsWith("t") && pid[1] >= '1' && pid[1] <= '4' && pid[2] == '.' ? pid[1] - '1' : -1);
        }
    sceneNorm.assign(allParams.size(), 0.0f);
    sceneReal.assign(allParams.size(), 0.0f);
    sceneMask.assign(allParams.size(), 0);
    startTimerHz(30);
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
    for (auto& m : modulators) m.reset();
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
    chaos.seed = seedOld;
    chaos.nextSeed = seedNew;
    chaos.switchBeats = seedSwitch;

    if (buffer.getNumChannels() >= 2) {
        // In chunks of at most 128 samples. Per chunk: run the modulators, build every track's settings
        // (knob value, + modulation, + macros), render each track into trackBuf, add it to the mix and,
        // through its SEND, into the shared Space. The Space keeps running when Live stops, so tails ring out.
        const int chunk = std::min(trackBuf.getNumSamples(), 128);
        std::array<mensis::LoopParams, kNumTracks> lps;
        for (int off = 0, n = 0; off < numSamples; off += n) {
            n = std::min(chunk, numSamples - off);
            const double chunkPpq = ppq + off * bpm / (60.0 * getSampleRate());

            // A recalled scene takes over exactly at the next bar line (shortcut: 4/4): end this chunk there,
            // so the next one starts on the bar. Stopped: right away.
            if (sceneState.load(std::memory_order_acquire) == 1) {
                const double nextBar = std::ceil(chunkPpq / 4.0 - 1e-9) * 4.0;
                const double toBar = (nextBar - chunkPpq) * 60.0 / bpm * getSampleRate();
                int expected = 1;
                if (!playing || toBar < 1.0) sceneState.compare_exchange_strong(expected, 2, std::memory_order_acq_rel);
                else if (toBar < n) n = static_cast<int>(std::ceil(toBar));
            }
            const bool sceneActive = sceneState.load(std::memory_order_acquire) == 2;
            auto sceneIndex = [&](const std::atomic<float>* raw) {
                if (!sceneActive) return -1;
                const auto it = rawIndex.find(raw);
                return it != rawIndex.end() && sceneMask[static_cast<size_t>(it->second)] ? it->second : -1;
            };

            // Modulation: each slot moves its target's normalised value by DEPTH x signal.
            std::array<std::atomic<float>*, kModSlots> modRaw{};
            std::array<float, kModSlots> modValue{};
            for (size_t m = 0; m < kModSlots; ++m) {
                const int idx = modTargetIndex[m].load(std::memory_order_relaxed);
                const auto& mp = modParams[m];
                const mensis::ModParams params{static_cast<mensis::ModType>(static_cast<int>(mp.type->load())),
                                               mp.sync->load() > 0.5f ? kModRateBeats[static_cast<size_t>(mp.rate->load())]
                                                                      : bpm / 60.0 / std::max(0.001f, mp.hz->load()), // Hz -> beats per cycle
                                               mp.shape->load(),
                                               seedNew, static_cast<uint32_t>(m)};
                const int track = idx >= 0 ? paramTrack[static_cast<size_t>(idx)] : -1;
                const float level = track >= 0 ? trackLevel[static_cast<size_t>(track)] : mixLevel;
                const float v = modulators[m].next(params, chunkPpq, level, n / getSampleRate(), bpm);
                modSignal[m].store(v, std::memory_order_relaxed);
                if (idx < 0) continue;
                auto* target = allParams[static_cast<size_t>(idx)];
                const bool fromScene = sceneActive && sceneMask[static_cast<size_t>(idx)];
                const float base = fromScene ? sceneNorm[static_cast<size_t>(idx)] : target->getValue();
                const float norm = std::clamp(base + mp.depth->load() * v, 0.0f, 1.0f);
                modRaw[m] = allRaw[static_cast<size_t>(idx)];
                modValue[m] = target->convertFrom0to1(norm);
                modNorm[m].store(norm, std::memory_order_relaxed);
            }
            auto val = [&](const std::atomic<float>* raw) { // knob value, its scene value, or its modulated value
                for (size_t m = 0; m < kModSlots; ++m)
                    if (modRaw[m] == raw) return modValue[m];
                if (const int i = sceneIndex(raw); i >= 0) return sceneReal[static_cast<size_t>(i)];
                return raw->load();
            };

            chaos.gridBeats = kGridBeats[static_cast<size_t>(val(chaosGrid))];
            const auto feel = static_cast<int>(val(chaosFeel));
            const mensis::Macros macros{val(macroParams[0]), val(macroParams[1]), val(macroParams[2]), val(macroParams[3])};
            for (size_t t = 0; t < kNumTracks; ++t) {
                const auto& tp = trackParams[t];
                auto& lp = lps[t];
                lp = {val(tp.start), val(tp.length), kBarValues[static_cast<size_t>(val(tp.bars))],
                      val(tp.reverse) > 0.5f, val(tp.level), val(tp.pan), val(tp.warp) > 0.5f, chaos};
                lp.chaos.amount = val(chaosAmount) * val(tp.chaos);
                lp.chaos.looseness = feel == 0 ? 0.0f : feel == 1 ? lp.chaos.amount : 1.0f; // Loose: drifts more as Chaos rises
                lp.chaos.stream = static_cast<uint32_t>(t);
                lp.pitch = val(tp.pitch);
                lp.slice = val(tp.slice) > 0.5f;
                lp.beat = val(tp.beat) > 0.5f;
                lp.swing = (val(swingParam) - 50.0f) / 50.0f; // 50% = straight, 75% = 0.5
                lp.on = val(tp.on) > 0.5f;
                lp.res = {val(tp.resMix), val(tp.resRoot), static_cast<int>(val(tp.resChord)), val(tp.resDecay),
                          val(tp.resColor), midiNotes, numMidiNotes};
                lp.filter = {val(tp.fltCutoff), val(tp.fltReso), val(tp.fltMorph)};
                lp.color = {val(tp.colDrive), val(tp.colChar), val(tp.colBits), val(tp.colRate),
                            val(tp.colSquash), val(tp.colTilt), val(tp.colOut), val(tp.colMix)};
                lp.grains = {val(tp.grainMix), val(tp.grainSize), val(tp.grainDensity), val(tp.grainSpray),
                             val(tp.grainDetune), val(tp.grainReverse), val(tp.grainWidth),
                             val(tp.grainFreeze) > 0.5f, lp.chaos.amount, seedNew, static_cast<uint32_t>(t)};
                float send = val(sendParams[t]);
                mensis::applyMacros(macros, lp, send);
                if (lp.color.rateHz >= 48000.0f) lp.color.rateHz = 1e9f; // knob at the top = off, at any host rate
                sendGain[t].setTargetValue(send);
            }
            mensis::SpaceParams sp{kDelayBeats[static_cast<size_t>(val(spaceTime))], val(spaceFeedback), val(spaceBlend),
                                   val(spaceSize), val(spaceTone), juce::Decibels::decibelsToGain(val(spaceReturn), -60.0f),
                                   val(spaceFreeze) > 0.5f};
            mensis::applyMacros(macros, sp);

            sendBuf.clear(0, n);
            for (size_t t = 0; t < kNumTracks && playing; ++t) {
                const auto* sample = liveSamples[t].load(std::memory_order_acquire);
                if (sample == nullptr) { trackLevel[t] = 0.0f; continue; }
                trackBuf.clear(0, n);
                tracks[t].render(*sample, lps[t], chunkPpq, bpm, trackBuf, 0, n);
                trackLevel[t] = std::max(trackBuf.getMagnitude(0, 0, n), trackBuf.getMagnitude(1, 0, n)); // feeds Env mods
                for (int ch = 0; ch < 2; ++ch) buffer.addFrom(ch, off, trackBuf, ch, 0, n);
                for (int i = 0; i < n; ++i) {
                    const float g = sendGain[t].getNextValue();
                    sendBuf.addSample(0, i, trackBuf.getSample(0, i) * g);
                    sendBuf.addSample(1, i, trackBuf.getSample(1, i) * g);
                }
            }
            space.process(sendBuf.getWritePointer(0), sendBuf.getWritePointer(1), n, sp, bpm);
            for (int ch = 0; ch < 2; ++ch) buffer.addFrom(ch, off, sendBuf, ch, 0, n);
            mixLevel = std::max(buffer.getMagnitude(0, off, n), buffer.getMagnitude(1, off, n));
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

void MensisProcessor::restoreSample(size_t track, const juce::File& file, const juce::MemoryBlock* embedded) {
    // The original file first (fast, and follows edits to it); else the copy saved in the set; else missing.
    if (file != juce::File{} && file.existsAsFile() && loadSample(track, file, false)) return;
    if (embedded != nullptr && embedded->getSize() > 0)
        if (auto sample = mensis::loadSampleFromData(*embedded, file)) {
            swapIn(track, std::move(sample));
            missingPaths[track] = {};
            return;
        }
    clearSample(track);
    missingPaths[track] = file.getFullPathName(); // shown as MISSING; LOAD fixes it
}

bool MensisProcessor::loadSample(size_t track, const juce::File& file, bool fresh) {
    auto sample = mensis::loadSample(file);
    if (sample == nullptr) return false;
    missingPaths[track] = {};

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

void MensisProcessor::clearSample(size_t track) {
    swapIn(track, nullptr);
    missingPaths[track] = {};
}

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

// Saved state: "MNS2" + a binary ValueTree (params, sample paths, mod targets, scenes, and each sample's file
// bytes when small enough, so the set still works if the file moves). Older sets were plain XML.
static constexpr char kStateMagic[4] = {'M', 'N', 'S', '2'};

void MensisProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = params.copyState();
    state.removeChild(state.getChildWithName("SAMPLES"), nullptr);
    juce::ValueTree samples("SAMPLES");
    for (size_t t = 0; t < kNumTracks; ++t) {
        const auto* sample = ownedSamples[t].get();
        // A missing sample keeps its path, so saving again doesn't forget where it was.
        state.setProperty(paramId(t, "file"), sample != nullptr ? sample->file.getFullPathName() : missingPaths[t], nullptr);
        if (sample != nullptr && sample->fileData.getSize() > 0) {
            juce::ValueTree s("SAMPLE");
            s.setProperty("track", static_cast<int>(t), nullptr);
            s.setProperty("data", juce::var(sample->fileData), nullptr);
            samples.appendChild(s, nullptr);
        }
    }
    state.appendChild(samples, nullptr);
    for (size_t m = 0; m < kModSlots; ++m)
        state.setProperty("mod" + juce::String(m + 1) + ".target", getModTarget(m), nullptr);
    state.setProperty("uiScale", uiScale, nullptr);
    state.removeChild(state.getChildWithName("SCENES"), nullptr);
    juce::ValueTree sceneTree("SCENES");
    for (int slot = 0; slot < kScenes; ++slot) {
        if (!hasScene(slot)) continue;
        juce::ValueTree scene("SCENE");
        scene.setProperty("slot", slot, nullptr);
        const auto& values = *scenes[static_cast<size_t>(slot)];
        for (size_t i = 0; i < values.size(); ++i)
            if (values[i]) scene.setProperty(allParams[i]->getParameterID(), *values[i], nullptr); // by ID: survives new params
        sceneTree.appendChild(scene, nullptr);
    }
    state.appendChild(sceneTree, nullptr);
    juce::MemoryOutputStream out(dest, false);
    out.write(kStateMagic, sizeof(kStateMagic));
    state.writeToStream(out);
}

void MensisProcessor::setStateInformation(const void* data, int sizeInBytes) {
    juce::ValueTree state;
    if (sizeInBytes > 4 && std::memcmp(data, kStateMagic, sizeof(kStateMagic)) == 0)
        state = juce::ValueTree::readFromData(static_cast<const char*>(data) + 4, static_cast<size_t>(sizeInBytes - 4));
    else if (auto xml = getXmlFromBinary(data, sizeInBytes)) // sets saved before samples were embedded
        state = juce::ValueTree::fromXml(*xml);
    if (!state.hasType(params.state.getType())) return;

    auto forParams = state.createCopy(); // the sample bytes don't belong in the live parameter tree
    forParams.removeChild(forParams.getChildWithName("SAMPLES"), nullptr);
    params.replaceState(forParams);
    for (size_t t = 0; t < kNumTracks; ++t) {
        const juce::File file(state.getProperty(paramId(t, "file")).toString());
        const juce::MemoryBlock* embedded = nullptr;
        for (const auto& s : state.getChildWithName("SAMPLES"))
            if (static_cast<int>(s.getProperty("track", -1)) == static_cast<int>(t)) embedded = s.getProperty("data").getBinaryData();
        restoreSample(t, file, embedded);
    }
    for (size_t m = 0; m < kModSlots; ++m)
        setModTarget(m, state.getProperty("mod" + juce::String(m + 1) + ".target").toString());
    uiScale = std::clamp(static_cast<double>(state.getProperty("uiScale", 1.0)), 0.6, 1.6);
    for (auto& scene : scenes) scene.reset();
    for (const auto& scene : state.getChildWithName("SCENES")) {
        const int slot = scene.getProperty("slot", -1);
        if (slot < 0 || slot >= kScenes) continue;
        std::vector<std::optional<float>> values(allParams.size());
        for (size_t i = 0; i < allParams.size(); ++i)
            if (const auto* v = scene.getPropertyPointer(allParams[i]->getParameterID())) values[i] = static_cast<float>(*v);
        scenes[static_cast<size_t>(slot)] = std::move(values);
    }
}

void MensisProcessor::setModTarget(size_t slot, const juce::String& paramIdToTarget) {
    int idx = -1;
    for (size_t i = 0; i < allParams.size(); ++i)
        if (allParams[i]->getParameterID() == paramIdToTarget) idx = static_cast<int>(i);
    modTargetIndex[slot].store(idx, std::memory_order_relaxed);
}

juce::String MensisProcessor::getModTarget(size_t slot) const {
    const int idx = modTargetIndex[slot].load(std::memory_order_relaxed);
    return idx >= 0 ? allParams[static_cast<size_t>(idx)]->getParameterID() : juce::String();
}

std::optional<float> MensisProcessor::getModulatedValue(const juce::RangedAudioParameter& p) const {
    for (size_t m = 0; m < kModSlots; ++m) {
        const int idx = modTargetIndex[m].load(std::memory_order_relaxed);
        if (idx >= 0 && allParams[static_cast<size_t>(idx)] == &p && modParams[m].depth->load() != 0.0f)
            return modNorm[m].load(std::memory_order_relaxed);
    }
    return std::nullopt;
}

void MensisProcessor::saveScene(int slot) {
    std::vector<std::optional<float>> values(allParams.size());
    for (size_t i = 0; i < allParams.size(); ++i)
        if (allParams[i]->getParameterID() != "master") values[i] = allParams[i]->getValue(); // volume stays yours
    scenes[static_cast<size_t>(slot)] = std::move(values);
    currentScene = slot;
}

void MensisProcessor::clearScene(int slot) {
    scenes[static_cast<size_t>(slot)].reset();
    if (currentScene == slot) currentScene = -1;
    if (queuedSlot == slot) queuedSlot = -1;
}

void MensisProcessor::recallScene(int slot) {
    if (!hasScene(slot)) return;
    queuedSlot = slot;
    handOverScene();
}

int MensisProcessor::getPendingScene() const {
    return queuedSlot >= 0 ? queuedSlot : sceneState.load() != 0 ? pendingSlot : -1;
}

void MensisProcessor::handOverScene() {
    if (queuedSlot < 0) return;
    // Write only while the audio thread isn't using the buffers: idle, or pending but not yet taken.
    int expected = 0;
    if (!sceneState.compare_exchange_strong(expected, 3, std::memory_order_acq_rel)) {
        expected = 1;
        if (!sceneState.compare_exchange_strong(expected, 3, std::memory_order_acq_rel)) return; // active: retry on the timer
    }
    const auto& values = *scenes[static_cast<size_t>(queuedSlot)];
    for (size_t i = 0; i < allParams.size(); ++i) {
        sceneMask[i] = values[i].has_value();
        if (!values[i]) continue;
        sceneNorm[i] = *values[i];
        sceneReal[i] = allParams[i]->convertFrom0to1(*values[i]);
    }
    pendingSlot = queuedSlot;
    queuedSlot = -1;
    sceneState.store(1, std::memory_order_release);
}

void MensisProcessor::updateScenes() {
    if (sceneState.load(std::memory_order_acquire) == 2) {
        // The audio thread is already playing the scene; now make the knobs (and Live) show it too.
        for (size_t i = 0; i < allParams.size(); ++i)
            if (sceneMask[i] && allParams[i]->getValue() != sceneNorm[i]) allParams[i]->setValueNotifyingHost(sceneNorm[i]);
        currentScene = pendingSlot;
        pendingSlot = -1;
        sceneState.store(0, std::memory_order_release);
    }
    handOverScene();
}

juce::AudioProcessorEditor* MensisProcessor::createEditor() { return new MensisEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MensisProcessor(); }
