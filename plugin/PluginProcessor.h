#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <vector>
#include "core/engine/LoopTrack.h"

// Thin host wrapper: reads host tempo/position and params, hands them to core/.
class MensisProcessor final : public juce::AudioProcessor {
public:
    static constexpr size_t kNumTracks = 4;

    MensisProcessor();

    // Message thread only. fresh: a new load by the user (not a saved set): pick the bar count that fits the
    // sample at the current tempo, and turn SLICE on if it sounds like drums.
    bool loadSample(size_t track, const juce::File& file, bool fresh);
    juce::File getSampleFile(size_t track) const; // message thread only
    const mensis::Sample* getSample(size_t track) const { return ownedSamples[track].get(); } // message thread only
    // 0..1 where the track is playing in its sample, -1 when stopped or empty. Any thread.
    float getPlayPosition(size_t track) const { return playPos[track].load(std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState params;

    const juce::String getName() const override { return JucePlugin_Name; }
    void prepareToPlay(double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    struct TrackParams {
        std::atomic<float>* start;
        std::atomic<float>* length;
        std::atomic<float>* bars; // choice index
        std::atomic<float>* reverse;
        std::atomic<float>* level;
        std::atomic<float>* pan;
        std::atomic<float>* warp;
        std::atomic<float>* chaos;
        std::atomic<float>* pitch;
        std::atomic<float>* slice;
    };
    std::array<TrackParams, kNumTracks> trackParams{};
    std::array<mensis::LoopTrack, kNumTracks> tracks;

    // The audio thread only reads these pointers. The message thread owns the Samples and never
    // frees one the audio thread might still be using (see loadSample).
    std::array<std::atomic<const mensis::Sample*>, kNumTracks> liveSamples{};
    std::array<std::unique_ptr<mensis::Sample>, kNumTracks> ownedSamples;
    struct Retired { std::unique_ptr<mensis::Sample> sample; uint64_t retiredAtBlock; };
    std::vector<Retired> retired;
    std::atomic<uint64_t> blocksDone{0};

    std::array<std::atomic<float>, kNumTracks> playPos{};
    std::atomic<float>* chaosAmount = nullptr;
    std::atomic<float>* chaosGrid = nullptr;
    std::atomic<float>* chaosFeel = nullptr;
    std::atomic<float>* seedParam = nullptr;
    uint32_t seedOld = 0, seedNew = 0; // audio thread only
    double seedSwitch = -1e300;
    bool seedStarted = false;
    std::atomic<double> lastBpm{120.0};
    double internalPpq = 0.0; // clock for Standalone, where there is no host
};
