// Mensis FX: the Mensis effect rack as an audio effect for any track. Thin host wrapper around mensis::FxRack.
#include <juce_audio_utils/juce_audio_utils.h>
#include "core/engine/FxRack.h"
#include "Params.h"
#include "FxEditor.h"

namespace {

using namespace mensis::params;

juce::AudioProcessorValueTreeState::ParameterLayout makeLayout() {
    Layout layout;
    // Push banks of 8: Grains, Resonator + Filter, Color, Space.
    addGrainParams(layout, "", "");
    addResFilterParams(layout, "", "");
    addColorParams(layout, "", "");
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"space.send", 1}, "Space Send",
                                                           juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f, percentText()));
    addSpaceParams(layout);
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"chaos", 1}, "Grain Chaos",
                                                           juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f, percentText()),
               std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"seed", 1}, "Seed", 0, 9999, 1));
    return layout;
}

class FxProcessor final : public juce::AudioProcessor {
public:
    FxProcessor()
        : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          params(*this, nullptr, "MensisFX", makeLayout()) {
        // Look the params up once: building a juce::String on the audio thread would allocate.
        for (auto& [slot, id] : std::initializer_list<std::pair<std::atomic<float>**, const char*>>{
                 {&grainMix, "grain.mix"}, {&grainSize, "grain.size"}, {&grainDensity, "grain.density"},
                 {&grainSpray, "grain.spray"}, {&grainDetune, "grain.detune"}, {&grainReverse, "grain.reverse"},
                 {&grainWidth, "grain.width"}, {&grainFreeze, "grain.freeze"}, {&chaos, "chaos"}, {&seed, "seed"},
                 {&resMix, "res.mix"}, {&resRoot, "res.root"}, {&resChord, "res.chord"}, {&resDecay, "res.decay"},
                 {&resColor, "res.color"}, {&fltCutoff, "flt.cutoff"}, {&fltReso, "flt.reso"}, {&fltMorph, "flt.morph"},
                 {&colDrive, "col.drive"}, {&colChar, "col.char"}, {&colBits, "col.bits"}, {&colRate, "col.rate"},
                 {&colSquash, "col.squash"}, {&colTilt, "col.tilt"}, {&colOut, "col.out"}, {&colMix, "col.mix"},
                 {&send, "space.send"}, {&spaceTime, "space.time"}, {&spaceFeedback, "space.feedback"},
                 {&spaceBlend, "space.blend"}, {&spaceSize, "space.size"}, {&spaceTone, "space.tone"},
                 {&spaceReturn, "space.return"}, {&spaceFreeze, "space.freeze"}})
            *slot = params.getRawParameterValue(id);
    }

    void prepareToPlay(double sampleRate, int maxBlock) override { rack.prepare(sampleRate, maxBlock); }
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
               && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples();
        double bpm = 120.0;
        if (auto* playHead = getPlayHead())
            if (auto pos = playHead->getPosition())
                if (auto b = pos->getBpm()) bpm = *b;

        auto v = [](const std::atomic<float>* p) { return p->load(std::memory_order_relaxed); };
        mensis::FxParams p;
        p.grains = {v(grainMix), v(grainSize), v(grainDensity), v(grainSpray), v(grainDetune), v(grainReverse),
                    v(grainWidth), v(grainFreeze) > 0.5f, v(chaos), static_cast<uint32_t>(v(seed)), 0};
        p.res = {v(resMix), v(resRoot), static_cast<int>(v(resChord)), v(resDecay), v(resColor), {}, 0};
        p.filter = {v(fltCutoff), v(fltReso), v(fltMorph)};
        p.color = {v(colDrive), v(colChar), v(colBits), v(colRate), v(colSquash), v(colTilt), v(colOut), v(colMix)};
        if (p.color.rateHz >= 48000.0f) p.color.rateHz = 1e9f; // knob at the top = off, at any host rate
        p.send = v(send);
        p.space = {kDelayBeats[static_cast<size_t>(v(spaceTime))], v(spaceFeedback), v(spaceBlend), v(spaceSize),
                   v(spaceTone), juce::Decibels::decibelsToGain(v(spaceReturn), -60.0f), v(spaceFreeze) > 0.5f};

        rack.process(buffer.getWritePointer(0), buffer.getWritePointer(1), n, p, bpm);
        for (int ch = 0; ch < 2; ++ch) // safety net: never send more than 0 dBFS to Live
            juce::FloatVectorOperations::clip(buffer.getWritePointer(ch), buffer.getReadPointer(ch), -1.0f, 1.0f, n);
    }

    juce::AudioProcessorEditor* createEditor() override { return new FxEditor(*this, params); }
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; } // longest Space reverb
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override {
        if (auto xml = params.copyState().createXml()) copyXmlToBinary(*xml, dest);
    }
    void setStateInformation(const void* data, int size) override {
        if (auto xml = getXmlFromBinary(data, size); xml != nullptr && xml->hasTagName(params.state.getType()))
            params.replaceState(juce::ValueTree::fromXml(*xml));
    }

private:
    juce::AudioProcessorValueTreeState params;
    std::atomic<float> *grainMix{}, *grainSize{}, *grainDensity{}, *grainSpray{}, *grainDetune{}, *grainReverse{},
        *grainWidth{}, *grainFreeze{}, *chaos{}, *seed{}, *resMix{}, *resRoot{}, *resChord{}, *resDecay{}, *resColor{},
        *fltCutoff{}, *fltReso{}, *fltMorph{}, *colDrive{}, *colChar{}, *colBits{}, *colRate{}, *colSquash{}, *colTilt{},
        *colOut{}, *colMix{}, *send{}, *spaceTime{}, *spaceFeedback{}, *spaceBlend{}, *spaceSize{}, *spaceTone{},
        *spaceReturn{}, *spaceFreeze{};
    mensis::FxRack rack;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxProcessor)
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FxProcessor(); }
