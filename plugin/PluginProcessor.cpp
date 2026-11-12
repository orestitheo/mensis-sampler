#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {

const juce::StringArray kBarNames{"1/4", "1/2", "1", "2", "4", "8", "16"};
constexpr std::array<double, 7> kBarValues{0.25, 0.5, 1, 2, 4, 8, 16};
const juce::StringArray kGridNames{"1/32", "1/16", "1/8", "1/4"};
constexpr std::array<double, 4> kGridBeats{0.125, 0.25, 0.5, 1.0};
const juce::StringArray kFeelNames{"Grid", "Loose", "Free"};

// Param IDs are stable: never rename once used in Live (automation and presets would break).
juce::String paramId(size_t track, const char* name) { return "t" + juce::String(track + 1) + "." + name; }

juce::AudioProcessorValueTreeState::ParameterLayout makeLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // One track's params per group, in order, so Push banks of 8 line up with tracks.
    for (size_t t = 0; t < MensisProcessor::kNumTracks; ++t) {
        auto id = [t](const char* name) { return juce::ParameterID{paramId(t, name), 1}; };
        auto label = [t](const char* name) { return "T" + juce::String(t + 1) + " " + name; };
        layout.add(std::make_unique<juce::AudioParameterFloat>(id("start"), label("Start"), 0.0f, 1.0f, 0.0f),
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
    return layout;
}

} // namespace

MensisProcessor::MensisProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "MensisSampler", makeLayout()) {
    for (size_t t = 0; t < kNumTracks; ++t) {
        auto raw = [&](const char* name) { return params.getRawParameterValue(paramId(t, name)); };
        trackParams[t] = {raw("start"), raw("length"), raw("bars"), raw("reverse"), raw("level"), raw("pan"), raw("warp"), raw("chaos"), raw("pitch")};
        playPos[t].store(-1.0f);
    }
    chaosAmount = params.getRawParameterValue("chaos");
    chaosGrid = params.getRawParameterValue("chaos.grid");
    chaosFeel = params.getRawParameterValue("chaos.feel");
    seedParam = params.getRawParameterValue("seed");
}

void MensisProcessor::prepareToPlay(double sampleRate, int) {
    for (auto& track : tracks) track.prepare(sampleRate);
}

void MensisProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
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
        for (auto& p : playPos) p.store(-1.0f, std::memory_order_relaxed);
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
    mensis::ChaosParams chaos;
    chaos.gridBeats = kGridBeats[static_cast<size_t>(chaosGrid->load())];
    const auto feel = static_cast<int>(chaosFeel->load());
    chaos.seed = seedOld;
    chaos.nextSeed = seedNew;
    chaos.switchBeats = seedSwitch;

    if (playing && buffer.getNumChannels() >= 2) {
        for (size_t t = 0; t < kNumTracks; ++t) {
            const auto* sample = liveSamples[t].load(std::memory_order_acquire);
            playPos[t].store(-1.0f, std::memory_order_relaxed);
            if (sample == nullptr) continue;
            const auto& tp = trackParams[t];
            mensis::LoopParams lp{tp.start->load(), tp.length->load(),
                                  kBarValues[static_cast<size_t>(tp.bars->load())], tp.reverse->load() > 0.5f,
                                  tp.level->load(), tp.pan->load(), tp.warp->load() > 0.5f, chaos};
            lp.chaos.amount = chaosAmount->load() * tp.chaos->load();
            lp.chaos.looseness = feel == 0 ? 0.0f : feel == 1 ? lp.chaos.amount : 1.0f; // Loose: drifts more as Chaos rises
            lp.chaos.stream = static_cast<uint32_t>(t);
            lp.pitch = tp.pitch->load();
            tracks[t].render(*sample, lp, ppq, bpm, buffer, 0, numSamples);
            playPos[t].store(tracks[t].position(), std::memory_order_relaxed);
        }
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) // safety net: never send more than 0 dBFS to Live
            juce::FloatVectorOperations::clip(buffer.getWritePointer(ch), buffer.getReadPointer(ch), -1.0f, 1.0f, numSamples);
    }
    internalPpq = ppq + numSamples * bpm / (60.0 * getSampleRate());
    blocksDone.fetch_add(1, std::memory_order_release);
}

bool MensisProcessor::loadSample(size_t track, const juce::File& file, bool guessBars) {
    auto sample = mensis::loadSample(file);
    if (sample == nullptr) return false;

    if (guessBars) {
        const double bars = sample->lengthSec() * lastBpm.load() / 240.0; // 4 beats per bar
        size_t best = 0;
        for (size_t i = 1; i < kBarValues.size(); ++i)
            if (std::abs(std::log2(kBarValues[i] / bars)) < std::abs(std::log2(kBarValues[best] / bars))) best = i;
        auto* p = params.getParameter(paramId(track, "bars"));
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(best)));
    }

    // Swap in the new sample. The old one may still be in use by the block that is running right now,
    // so keep it until the audio thread has finished at least one more full block.
    liveSamples[track].store(sample.get(), std::memory_order_release);
    if (ownedSamples[track] != nullptr)
        retired.push_back({std::move(ownedSamples[track]), blocksDone.load(std::memory_order_acquire)});
    ownedSamples[track] = std::move(sample);

    const auto done = blocksDone.load(std::memory_order_acquire);
    std::erase_if(retired, [done](const Retired& r) { return done > r.retiredAtBlock + 1; });
    return true;
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
    }
}

juce::AudioProcessorEditor* MensisProcessor::createEditor() { return new MensisEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MensisProcessor(); }
