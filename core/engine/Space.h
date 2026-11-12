#pragma once
#include <array>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

namespace mensis {

struct SpaceParams {
    double timeBeats = 0.75; // delay time in beats (synced to tempo)
    float feedback = 0.4f;   // 0..0.95
    float blend = 0.5f;      // 0 = delay only, 1 = reverb only
    float size = 0.5f;       // 0..1: room size and decay (~0.5 s .. 20 s)
    float tone = 0.5f;       // 0 = dark .. 1 = bright
    float level = 1.0f;      // output gain (linear)
    bool freeze = false;     // hold whatever is in there, forever
};

// Shared delay + reverb. All memory is allocated in prepare(); process() is real-time safe.
class Space {
public:
    void prepare(double sampleRate);
    // l/r hold the send signal on the way in and the Space output on the way out.
    void process(float* l, float* r, int numSamples, const SpaceParams& p, double bpm);

private:
    static constexpr int kLines = 8, kDiffusers = 4;
    static float readAt(const std::vector<float>& buf, int writePos, double delay);

    double sampleRate = 48000.0;
    bool primed = false;

    // delay
    std::array<std::vector<float>, 2> delayBuf;
    int delayPos = 0;
    std::array<float, 2> delayLow{};
    juce::SmoothedValue<float> delayTime; // samples, glides like tape

    // reverb
    std::array<std::vector<float>, kLines> lines;
    std::array<float, kLines> lineLow{};
    int linePos = 0;
    std::array<std::array<std::vector<float>, kDiffusers>, 2> diffusers;
    std::array<std::array<int, kDiffusers>, 2> diffPos{};
    double lfoPhase = 0.0;
    std::array<float, 2> dcLow{}; // 30 Hz high-pass at the reverb input: a long tail would pile up any DC

    juce::SmoothedValue<float> feedback, blend, size, tone, level;
};

} // namespace mensis
