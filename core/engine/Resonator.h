#pragma once
#include <array>
#include <juce_audio_basics/juce_audio_basics.h>

namespace mensis {

struct ResonatorParams {
    float mix = 0.0f;          // 0 = off (dry), 1 = only the resonance
    float root = 48.0f;        // MIDI note (60 = C3 in Live)
    int chord = 0;             // index into kChords
    float decay = 1.0f;        // seconds to ring out (T60)
    float color = 0.0f;        // 0 = string (harmonic partials), 1 = bell (free-bar partials)
    std::array<float, 4> midiNotes{}; // notes held on the MIDI input; they override root+chord
    int numMidiNotes = 0;
};

struct Chord { const char* name; std::array<int, 4> intervals; int count; };
inline constexpr std::array<Chord, 8> kChords{{
    {"Unison", {0, 0, 0, 0}, 1}, {"Octaves", {0, 12, 24, 0}, 3}, {"Fifths", {0, 7, 12, 19}, 4},
    {"Major", {0, 4, 7, 12}, 4}, {"Minor", {0, 3, 7, 12}, 4}, {"Sus4", {0, 5, 7, 12}, 4},
    {"Maj7", {0, 4, 7, 11}, 4}, {"Min7", {0, 3, 7, 10}, 4}}};

// A bank of tuned resonators: up to 4 notes x 4 partials, per channel. Real-time safe.
class Resonator {
public:
    void prepare(double sampleRate);
    void process(float& l, float& r, const ResonatorParams& p); // one stereo sample, in place

private:
    static constexpr int kNotes = 4, kPartials = 4, kModes = kNotes * kPartials, kUpdateEvery = 32;
    struct Mode {
        float b0 = 0.0f, a1 = 0.0f, a2 = 0.0f, gain = 0.0f; // band-pass: y = b0*(x - x2) - a1*y1 - a2*y2
        std::array<float, 2> x1{}, x2{}, y1{}, y2{};         // per channel
    };
    void updateCoefficients(const ResonatorParams& p);

    double sampleRate = 48000.0;
    std::array<Mode, kModes> modes{};
    std::array<float, kNotes> noteHz{}; // gliding toward the target notes
    int activeModes = 0, untilUpdate = 0;
    bool fresh = true;
    juce::SmoothedValue<float> mix;
};

struct FilterParams {
    float cutoff = 20000.0f; // Hz
    float reso = 0.0f;       // 0..1
    float morph = 0.0f;      // 0 = low-pass, 0.5 = band-pass, 1 = high-pass
    bool isDefault() const { return cutoff >= 20000.0f && reso <= 0.0f && morph <= 0.0f; }
};

// State-variable filter (Cytomic/Simper TPT form): stays stable while the cutoff is swept.
class MorphFilter {
public:
    void prepare(double sampleRate);
    void process(float& l, float& r, const FilterParams& p); // one stereo sample, in place

private:
    double sampleRate = 48000.0;
    std::array<float, 2> ic1{}, ic2{};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoff;
    juce::SmoothedValue<float> reso, morph;
    bool primed = false;
};

} // namespace mensis
