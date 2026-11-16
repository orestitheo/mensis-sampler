#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <vector>
#include "core/engine/LoopTrack.h"
#include "core/engine/Space.h"
#include "core/engine/Modulator.h"
#include <optional>
#include <unordered_map>

// Thin host wrapper: reads host tempo/position and params, hands them to core/.
class MensisProcessor final : public juce::AudioProcessor, private juce::Timer {
public:
    static constexpr size_t kNumTracks = 4;
    static constexpr size_t kModSlots = 4;
    static constexpr int kScenes = 8;

    MensisProcessor();

    // Message thread only. fresh: a new load by the user (not a saved set): pick the bar count that fits the
    // sample at the current tempo, and turn SLICE on if it sounds like drums.
    bool loadSample(size_t track, const juce::File& file, bool fresh);
    void clearSample(size_t track);               // message thread only
    // A sample the saved set pointed to that couldn't be found (and wasn't embedded); "" if none. Message thread.
    juce::String getMissingSample(size_t track) const { return missingPaths[track]; }
    juce::File getSampleFile(size_t track) const; // message thread only
    const mensis::Sample* getSample(size_t track) const { return ownedSamples[track].get(); } // message thread only
    // 0..1 where the track is playing in its sample, -1 when stopped or empty. Any thread.
    float getPlayPosition(size_t track) const { return playPos[track].load(std::memory_order_relaxed); }
    double getBars(size_t track) const; // the track's loop length in bars, any thread

    // Modulation. Targets are param IDs ("" = none); set/get on the message thread.
    void setModTarget(size_t slot, const juce::String& paramIdToTarget);
    juce::String getModTarget(size_t slot) const;
    float getModSignal(size_t slot) const { return modSignal[slot].load(std::memory_order_relaxed); } // any thread
    // The modulated value (0..1) of a knob that is a mod target with non-zero depth, for drawing.
    std::optional<float> getModulatedValue(const juce::RangedAudioParameter& p) const;

    // Scenes: snapshots of every knob except master volume. Message thread only.
    void saveScene(int slot);
    void recallScene(int slot); // takes over at the next bar (immediately when stopped)
    void clearScene(int slot);
    bool hasScene(int slot) const { return scenes[static_cast<size_t>(slot)].has_value(); }
    int getCurrentScene() const { return currentScene; }
    bool sceneChanged() const; // a knob has moved away from the current scene
    int getPendingScene() const; // waiting for the bar, -1 if none

    // Window size as a scale of the design size; saved with the set. Message thread.
    double getUiScale() const { return uiScale; }
    void setUiScale(double s) { uiScale = s; }
    void updateScenes();         // message thread, ~30x/s (own timer): finishes a recall the audio thread started

    // Grain cloud for drawing, any thread. env 0 = grain not playing; anchor -1 = cloud off.
    struct GrainDot { float x, env, pan; bool backwards; };
    GrainDot getGrainDot(size_t track, size_t i) const {
        const auto& d = grainDots[track][i];
        const float env = d.env.load(std::memory_order_relaxed);
        return {d.x.load(std::memory_order_relaxed), std::abs(env), d.pan.load(std::memory_order_relaxed), env < 0.0f};
    }
    float getGrainAnchor(size_t track) const { return grainAnchor[track].load(std::memory_order_relaxed); }
    static constexpr size_t kMaxGrains = mensis::GrainCloud::kMaxGrains;

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
    double getTailLengthSeconds() const override { return 20.0; } // longest Space reverb

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
        std::atomic<float>* grainMix;
        std::atomic<float>* grainSize;
        std::atomic<float>* grainDensity;
        std::atomic<float>* grainSpray;
        std::atomic<float>* grainDetune;
        std::atomic<float>* grainReverse;
        std::atomic<float>* grainWidth;
        std::atomic<float>* grainFreeze;
        std::atomic<float>* on;
        std::atomic<float>* resMix;
        std::atomic<float>* resRoot;
        std::atomic<float>* resChord;
        std::atomic<float>* resDecay;
        std::atomic<float>* resColor;
        std::atomic<float>* fltCutoff;
        std::atomic<float>* fltReso;
        std::atomic<float>* fltMorph;
        std::atomic<float>* colDrive;
        std::atomic<float>* colChar;
        std::atomic<float>* colBits;
        std::atomic<float>* colRate;
        std::atomic<float>* colSquash;
        std::atomic<float>* colTilt;
        std::atomic<float>* colOut;
        std::atomic<float>* colMix;
        std::atomic<float>* beat;
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
    struct AtomicDot { std::atomic<float> x{0.0f}, env{0.0f}, pan{0.0f}; }; // env < 0 = plays backwards
    std::array<std::array<AtomicDot, mensis::GrainCloud::kMaxGrains>, kNumTracks> grainDots;
    std::array<std::atomic<float>, kNumTracks> grainAnchor{};
    void publishGrains(size_t track, const mensis::Sample* sample, bool on);
    void swapIn(size_t track, std::unique_ptr<mensis::Sample> sample); // message thread
    void restoreSample(size_t track, const juce::File& file, const juce::MemoryBlock* embedded); // message thread
    std::array<juce::String, kNumTracks> missingPaths;
    std::atomic<float>* chaosAmount = nullptr;
    std::atomic<float>* chaosGrid = nullptr;
    std::atomic<float>* chaosFeel = nullptr;
    std::atomic<float>* seedParam = nullptr;
    std::atomic<float>* masterDb = nullptr;
    std::atomic<float>* swingParam = nullptr;
    struct ModSlotParams {
        std::atomic<float>* type; std::atomic<float>* rate; std::atomic<float>* shape; std::atomic<float>* depth;
        std::atomic<float>* sync; std::atomic<float>* hz; // SYNC off: RATE in Hz instead of note lengths
    };
    std::array<ModSlotParams, kModSlots> modParams{};
    std::array<std::atomic<float>*, 4> macroParams{};
    std::array<mensis::Modulator, kModSlots> modulators;
    std::array<std::atomic<int>, kModSlots> modTargetIndex{};
    std::array<std::atomic<float>, kModSlots> modSignal{}, modNorm{};
    // Scenes. scenes[] belongs to the message thread. sceneNorm/sceneReal/sceneMask (sized once, in the
    // constructor) carry one scene to the audio thread; sceneState says who may touch them:
    // 0 idle, 1 pending (waiting for the bar), 2 active (audio uses the values), 3 message thread writing.
    std::array<std::optional<std::vector<std::optional<float>>>, kScenes> scenes;
    std::vector<float> sceneNorm, sceneReal;
    std::vector<char> sceneMask;
    std::atomic<int> sceneState{0};
    int pendingSlot = -1, queuedSlot = -1, currentScene = -1; // message thread
    double uiScale = 1.0;
    void handOverScene();      // message thread: queued scene -> pending, when the audio thread allows
    void timerCallback() override { updateScenes(); }
    std::unordered_map<const std::atomic<float>*, int> rawIndex; // raw value -> allParams index (fixed)

    std::vector<juce::RangedAudioParameter*> allParams; // fixed after construction
    std::vector<std::atomic<float>*> allRaw;
    std::vector<int> paramTrack;
    std::array<float, kNumTracks> trackLevel{}; // audio thread: last chunk's peak per track, for Env mods
    float mixLevel = 0.0f;
    std::array<std::atomic<float>*, kNumTracks> sendParams{};
    std::atomic<float>* spaceTime = nullptr;
    std::atomic<float>* spaceFeedback = nullptr;
    std::atomic<float>* spaceBlend = nullptr;
    std::atomic<float>* spaceSize = nullptr;
    std::atomic<float>* spaceTone = nullptr;
    std::atomic<float>* spaceReturn = nullptr;
    std::atomic<float>* spaceFreeze = nullptr;
    mensis::Space space;
    juce::AudioBuffer<float> trackBuf, sendBuf; // scratch, sized in prepareToPlay
    std::array<juce::SmoothedValue<float>, kNumTracks> sendGain;
    juce::SmoothedValue<float> masterGain{1.0f};
    uint32_t seedOld = 0, seedNew = 0; // audio thread only
    double seedSwitch = -1e300;
    bool seedStarted = false;
    std::array<int, 16> heldNotes{}; // MIDI notes held, oldest first (audio thread only)
    int numHeld = 0;
    std::atomic<double> lastBpm{120.0};
    double internalPpq = 0.0; // clock for Standalone, where there is no host
};
