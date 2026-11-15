#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "core/engine/Resonator.h"

// Parameter definitions shared by Mensis Sampler and Mensis FX.
// Param IDs are stable: never rename once used in Live (automation and presets would break).
namespace mensis::params {

using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

inline const juce::StringArray kDelayNames{"1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/4D", "1/2", "1 bar"};
inline constexpr std::array<double, 12> kDelayBeats{0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75, 1.0, 1.5, 2.0, 4.0};

// How knobs read: 0..1 values as percent, levels in dB, pan as L/C/R. Typing works too ("50", "50%", "L 30").
inline juce::AudioParameterFloatAttributes percentText() {
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; })
        .withValueFromStringFunction([](const juce::String& s) { return s.retainCharacters("-0123456789.").getFloatValue() / 100.0f; });
}
inline juce::AudioParameterFloatAttributes dbText() {
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float v, int) { return v <= -60.0f ? juce::String("-inf") : juce::String(v, 1) + " dB"; })
        .withValueFromStringFunction([](const juce::String& s) { return s.containsIgnoreCase("inf") ? -60.0f : s.getFloatValue(); });
}
inline juce::AudioParameterFloatAttributes panText() {
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
inline juce::AudioParameterFloatAttributes text(std::function<juce::String(float, int)> fn) {
    return juce::AudioParameterFloatAttributes().withStringFromValueFunction(std::move(fn));
}

// Each group below is 8 params (one Push bank). idPrefix goes before every ID ("t1." in the sampler, "" in FX),
// labelPrefix before every name ("T1 " / "").

inline void addGrainParams(Layout& layout, const juce::String& idPrefix, const juce::String& labelPrefix) {
    auto id = [&](const char* name) { return juce::ParameterID{idPrefix + name, 1}; };
    auto label = [&](const char* name) { return labelPrefix + "Grain " + name; };
    auto range = [](float lo, float hi, float centre) {
        juce::NormalisableRange<float> r(lo, hi);
        r.setSkewForCentre(centre); // more knob travel for the small values
        return r;
    };
    const auto percent = percentText();
    layout.add(std::make_unique<juce::AudioParameterFloat>(id("grain.mix"), label("Mix"), juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f, percent),
               std::make_unique<juce::AudioParameterFloat>(id("grain.size"), label("Size"), range(10.0f, 500.0f, 80.0f), 80.0f,
                   text([](float v, int) { return juce::String(juce::roundToInt(v)) + " ms"; })),
               std::make_unique<juce::AudioParameterFloat>(id("grain.density"), label("Density"), range(1.0f, 100.0f, 20.0f), 20.0f,
                   text([](float v, int) { return juce::String(v, v < 10.0f ? 1 : 0) + " /s"; })),
               std::make_unique<juce::AudioParameterFloat>(id("grain.spray"), label("Spray"), juce::NormalisableRange<float>(0.0f, 1.0f), 0.2f, percent),
               std::make_unique<juce::AudioParameterFloat>(id("grain.detune"), label("Detune"), juce::NormalisableRange<float>(0.0f, 12.0f), 0.0f,
                   text([](float v, int) { return juce::String(v, 1) + " st"; })),
               std::make_unique<juce::AudioParameterFloat>(id("grain.reverse"), label("Reverse"), juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f, percent),
               std::make_unique<juce::AudioParameterFloat>(id("grain.width"), label("Width"), juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f, percent),
               std::make_unique<juce::AudioParameterBool>(id("grain.freeze"), label("Freeze"), false));
}

inline void addResFilterParams(Layout& layout, const juce::String& idPrefix, const juce::String& labelPrefix) {
    auto id = [&](const char* name) { return juce::ParameterID{idPrefix + name, 1}; };
    auto label = [&](const char* name) { return labelPrefix + name; };
    juce::StringArray chordNames;
    for (const auto& c : mensis::kChords) chordNames.add(c.name);
    const auto pct = percentText();
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

inline void addColorParams(Layout& layout, const juce::String& idPrefix, const juce::String& labelPrefix) {
    auto id = [&](const char* name) { return juce::ParameterID{idPrefix + name, 1}; };
    auto label = [&](const char* name) { return labelPrefix + name; };
    const auto pct = percentText();
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

// The Space settings (7 params, no prefix: Space is one shared unit in both plugins).
inline void addSpaceParams(Layout& layout) {
    const auto pct = percentText();
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
}

} // namespace mensis::params
