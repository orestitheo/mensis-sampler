#pragma once
#include "PluginProcessor.h"
#include "Look.h"

// One track's sample: loop region lit, playhead line, drag near an edge to move Start or End.
class WaveformView final : public juce::Component {
public:
    WaveformView(MensisProcessor& p, size_t t) : proc(p), track(t) {}

    void paint(juce::Graphics& g) override {
        const auto b = getLocalBounds().toFloat();
        g.setColour(MensisLook::dim);
        g.drawRect(b, 1.0f);
        const auto* sample = proc.getSample(track);
        if (sample == nullptr) {
            g.setColour(MensisLook::mid);
            g.setFont(MensisLook::font(11.0f));
            g.drawText("DROP A SAMPLE", b, juce::Justification::centred);
            return;
        }
        if (sample != mipFor) buildMip(*sample);
        if (v0 != colsV0 || v1 != colsV1 || static_cast<int>(cols.size()) != getWidth()) buildColumns(*sample);

        const auto [st, en] = region();
        const float w = b.getWidth(), h = b.getHeight(), mid = b.getCentreY(), half = h * 0.5f - 4.0f;
        const float sx = toX(st), ex = toX(en);

        // Loop region: a soft wash, brighter at the top.
        const float wash = grab == Grab::New ? 0.16f : 0.09f;
        g.setGradientFill(juce::ColourGradient(MensisLook::fg.withAlpha(wash), 0.0f, 0.0f,
                                               MensisLook::fg.withAlpha(wash * 0.25f), 0.0f, h, false));
        g.fillRect(juce::Rectangle<float>(sx, 0.0f, ex - sx, h).getIntersection(b));
        drawBeatTicks(g, sx, ex);
        g.setColour(MensisLook::dim.withAlpha(0.6f));
        g.drawHorizontalLine(static_cast<int>(mid), 0.0f, w); // centre line

        // Waveform: faint outline = peaks, solid core = body (RMS). Outside the loop it stays dark.
        const double spp = (v1 - v0) * (sample->audio.getNumSamples() - 1) / std::max(1.0f, w); // samples per pixel
        if (spp >= 1.5) {
            for (size_t x = 0; x < cols.size(); ++x) {
                const auto fx = static_cast<float>(x);
                const bool lit = fx >= sx && fx <= ex;
                g.setColour(lit ? MensisLook::fg.withAlpha(0.4f) : MensisLook::dim);
                g.drawVerticalLine(static_cast<int>(x), mid - cols[x].peak * half, mid + cols[x].peak * half);
                g.setColour(lit ? MensisLook::fg : MensisLook::mid.withAlpha(0.5f));
                g.drawVerticalLine(static_cast<int>(x), mid - cols[x].rms * half, mid + cols[x].rms * half);
            }
        } else {
            drawSampleLine(g, *sample, mid, half, sx, ex, spp);
        }

        if (value("slice") > 0.5f) { // hit markers inside the loop region: thin line + a tick at the bottom
            const float n = static_cast<float>(std::max(1, sample->audio.getNumSamples() - 1));
            for (int onset : sample->slices) {
                const float fx = static_cast<float>(onset) / n;
                if (fx <= st || fx >= en) continue; // only the region gets sliced
                const float x = toX(fx);
                g.setColour(MensisLook::fg.withAlpha(0.3f));
                g.drawVerticalLine(static_cast<int>(x), 0.0f, h);
                g.setColour(MensisLook::fg);
                g.fillRect(x - 0.5f, h - 6.0f, 2.0f, 6.0f);
            }
        }
        for (const auto [edge, x] : {std::pair{Grab::Start, sx}, std::pair{Grab::End, ex - 2.0f}}) {
            const bool active = grab == edge || (grab == Grab::None && hover == edge);
            g.setColour(active ? MensisLook::fg : MensisLook::fg.withAlpha(0.7f));
            g.fillRect(x, 0.0f, active ? 3.0f : 2.0f, h);
            // small tab at the top, pointing into the region, so the edges look grabbable
            g.fillRect(edge == Grab::Start ? x : x - 6.0f, 0.0f, 8.0f, 10.0f);
        }
        drawCloud(g, b, *sample);
        if (const float pos = proc.getPlayPosition(track); pos >= 0.0f) {
            // Playhead with a soft glow trailing behind it.
            const float x = toX(pos), trail = value("reverse") > 0.5f ? 28.0f : -28.0f;
            g.setGradientFill(juce::ColourGradient(MensisLook::fg.withAlpha(0.28f), x, 0.0f,
                                                   MensisLook::fg.withAlpha(0.0f), x + trail, 0.0f, false));
            g.fillRect(juce::Rectangle<float>(std::min(x, x + trail), 0.0f, std::abs(trail), h));
            g.setColour(MensisLook::fg);
            g.fillRect(x - 0.5f, 0.0f, 1.0f, h);
            g.fillEllipse(x - 3.0f, -3.0f, 6.0f, 6.0f);
        }
        if (v1 - v0 < 0.999) { // zoomed in: thin overview along the bottom, white = what you're looking at
            g.setColour(MensisLook::dim);
            g.fillRect(0.0f, h - 2.0f, w, 2.0f);
            g.setColour(MensisLook::mid);
            g.fillRect(st * w, h - 2.0f, (en - st) * w, 2.0f);
            g.setColour(MensisLook::fg);
            g.fillRect(static_cast<float>(v0) * w, h - 2.0f, std::max(2.0f, static_cast<float>(v1 - v0) * w), 2.0f);
        }
    }

    // Drag anywhere: the part you sweep over becomes the loop. Drag an edge: resize that side.
    // Double-click: the whole sample, zoomed out. Scroll/pinch: zoom around the mouse.
    // Sideways scroll or Shift+scroll: move along the sample.
    void mouseMove(const juce::MouseEvent& e) override {
        const Grab g = proc.getSample(track) == nullptr ? Grab::None : grabAt(e.position.x);
        setMouseCursor(g == Grab::Start || g == Grab::End ? juce::MouseCursor::LeftRightResizeCursor
                       : g == Grab::New ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor);
        if (g != hover) { hover = g; repaint(); }
    }

    void mouseExit(const juce::MouseEvent&) override { hover = Grab::None; repaint(); }

    void mouseDown(const juce::MouseEvent& e) override {
        if (proc.getSample(track) == nullptr) return;
        grab = grabAt(e.position.x);
        std::tie(downStart, downEnd) = region();
        downX = xNorm(e);
        param("start")->beginChangeGesture();
        param("length")->beginChangeGesture();
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        const float x = xNorm(e);
        switch (grab) {
            case Grab::None: return;
            case Grab::Start: apply(std::min(x, downEnd), downEnd); break;
            case Grab::End: apply(downStart, std::max(x, downStart)); break;
            case Grab::New:
                if (e.getDistanceFromDragStartX() == 0) return; // a plain click doesn't wipe the region
                apply(std::min(x, downX), std::max(x, downX));
                break;
        }
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (grab == Grab::None) return;
        param("start")->endChangeGesture();
        param("length")->endChangeGesture();
        grab = Grab::None;
    }

    void mouseDoubleClick(const juce::MouseEvent&) override {
        if (proc.getSample(track) == nullptr) return;
        param("start")->beginChangeGesture();
        param("length")->beginChangeGesture();
        apply(0.0f, 1.0f);
        param("start")->endChangeGesture();
        param("length")->endChangeGesture();
        setView(0.0, 1.0);
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
        if (proc.getSample(track) == nullptr) return;
        const float sideways = e.mods.isShiftDown() ? wheel.deltaY : wheel.deltaX;
        if (std::abs(sideways) > std::abs(e.mods.isShiftDown() ? 0.0f : wheel.deltaY)) {
            const double span = v1 - v0, shift = -sideways * span;
            setView(v0 + shift, v1 + shift);
        } else {
            zoomAround(e.position.x, std::exp(-wheel.deltaY * 2.0));
        }
    }

    void mouseMagnify(const juce::MouseEvent& e, float scale) override {
        if (proc.getSample(track) != nullptr) zoomAround(e.position.x, 1.0 / std::max(0.01f, scale));
    }

private:
    juce::RangedAudioParameter* param(const char* name) const {
        return proc.params.getParameter("t" + juce::String(track + 1) + "." + name);
    }
    float value(const char* name) const { return param(name)->convertFrom0to1(param(name)->getValue()); }
    void set(const char* name, float v) { param(name)->setValueNotifyingHost(param(name)->convertTo0to1(v)); }
    std::pair<float, float> region() const {
        const float st = value("start");
        return {st, std::min(1.0f, st + value("length"))};
    }

    // --- zoom: the view shows [v0, v1] of the sample (0..1) ---
    float toX(float norm) const { return static_cast<float>((norm - v0) / (v1 - v0)) * static_cast<float>(getWidth()); }
    float fromX(float px) const {
        return std::clamp(static_cast<float>(v0 + px / std::max(1, getWidth()) * (v1 - v0)), 0.0f, 1.0f);
    }
    void zoomAround(float px, double factor) {
        const auto* sample = proc.getSample(track);
        const double c = fromX(px), span = v1 - v0;
        // Closest zoom: 10 pixels per sample.
        const double minSpan = std::min(1.0, 0.1 * getWidth() / std::max(1, sample->audio.getNumSamples() - 1));
        const double next = std::clamp(span * factor, minSpan, 1.0);
        const double a = c - (c - v0) * next / span;
        setView(a, a + next);
    }
    void setView(double a, double z) {
        const double span = z - a;
        a = std::clamp(a, 0.0, 1.0 - span); // slide back inside the sample
        v0 = a;
        v1 = a + span;
        repaint();
    }

    void drawBeatTicks(juce::Graphics& g, float sx, float ex) const {
        // One tick per beat across the top of the loop region, longer on each bar.
        const double beats = proc.getBars(track) * 4.0;
        const double px = (ex - sx) / beats;
        if (px < 3.0) return; // too dense to read
        for (int k = 1; k < static_cast<int>(beats); ++k) {
            const float x = sx + static_cast<float>(k * px);
            const bool bar = k % 4 == 0;
            g.setColour(MensisLook::fg.withAlpha(bar ? 0.7f : 0.35f));
            g.fillRect(x - 0.5f, 0.0f, 1.0f, bar ? 7.0f : 4.0f);
        }
    }

    void drawSampleLine(juce::Graphics& g, const mensis::Sample& sample, float mid, float half, float sx, float ex,
                        double spp) const {
        // Zoomed all the way in: the actual wave, as a line through the samples (dots on each sample when close).
        const int n = sample.audio.getNumSamples();
        const int a = std::max(0, static_cast<int>(v0 * (n - 1)) - 1), z = std::min(n - 1, static_cast<int>(v1 * (n - 1)) + 2);
        const int chs = sample.audio.getNumChannels();
        auto valueAt = [&](int i) {
            float m = 0.0f;
            for (int ch = 0; ch < chs; ++ch) m += sample.audio.getSample(ch, i);
            return m / static_cast<float>(chs);
        };
        auto pointAt = [&](int i) {
            return juce::Point<float>(toX(static_cast<float>(i) / static_cast<float>(n - 1)), mid - valueAt(i) * half);
        };
        juce::Path line;
        line.startNewSubPath(pointAt(a));
        for (int i = a + 1; i <= z; ++i) line.lineTo(pointAt(i));
        g.setColour(MensisLook::dim);
        g.strokePath(line, juce::PathStrokeType(1.5f));
        g.saveState();
        g.reduceClipRegion(juce::Rectangle<float>(sx, 0.0f, ex - sx, static_cast<float>(getHeight())).toNearestInt());
        g.setColour(MensisLook::fg);
        g.strokePath(line, juce::PathStrokeType(1.5f));
        if (spp < 0.25)
            for (int i = a; i <= z; ++i) {
                const auto p = pointAt(i);
                g.fillEllipse(p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f);
            }
        g.restoreState();
    }

    // The grain cloud: one dot per grain at the spot it reads, higher = panned left, lower = right.
    // Dots swell and fade with the grain's window, glow softly and leave a short trail; backwards grains
    // are rings. Under it: the spray range as a thin bar, and when frozen a dotted line with a diamond on top.
    void drawCloud(juce::Graphics& g, juce::Rectangle<float> b, const mensis::Sample& sample) const {
        const float anchor = proc.getGrainAnchor(track);
        if (anchor < 0.0f) return; // cloud off
        const float h = b.getHeight(), mid = b.getCentreY();

        const float ax = toX(anchor);
        const float spray = value("grain.spray") * 0.5f * static_cast<float>(sample.sampleRate)
                            / static_cast<float>(std::max(1, sample.audio.getNumSamples()))
                            / static_cast<float>(v1 - v0) * b.getWidth();
        g.setColour(MensisLook::fg.withAlpha(0.25f));
        g.fillRect(juce::Rectangle<float>(ax - spray, h - 6.0f, 2.0f * spray, 1.0f).getIntersection(b));
        g.setColour(MensisLook::fg.withAlpha(0.6f));
        g.fillRect(ax - 0.5f, h - 9.0f, 1.0f, 5.0f); // where the cloud is centred

        if (value("grain.freeze") > 0.5f) {
            g.setColour(MensisLook::fg);
            for (float y = 10.0f; y < h - 10.0f; y += 5.0f) g.fillRect(ax - 0.5f, y, 1.0f, 2.0f);
            juce::Path diamond;
            diamond.addQuadrilateral(ax, 1.0f, ax + 4.0f, 5.0f, ax, 9.0f, ax - 4.0f, 5.0f);
            g.fillPath(diamond);
        }

        for (size_t i = 0; i < MensisProcessor::kMaxGrains; ++i) {
            const auto d = proc.getGrainDot(track, i);
            if (d.env <= 0.01f) continue;
            const float x = toX(d.x), y = mid - d.pan * (h * 0.5f - 10.0f);
            const float r = 1.5f + 2.5f * d.env;
            // halo
            g.setGradientFill(juce::ColourGradient(MensisLook::fg.withAlpha(0.18f * d.env), x, y,
                                                   MensisLook::fg.withAlpha(0.0f), x + r * 3.5f, y, true));
            g.fillEllipse(x - r * 3.5f, y - r * 3.5f, r * 7.0f, r * 7.0f);
            // trail
            const float trail = 12.0f * d.env * (d.backwards ? 1.0f : -1.0f);
            g.setGradientFill(juce::ColourGradient(MensisLook::fg.withAlpha(0.5f * d.env), x, y,
                                                   MensisLook::fg.withAlpha(0.0f), x + trail, y, false));
            g.fillRect(juce::Rectangle<float>(std::min(x, x + trail), y - 0.5f, std::abs(trail), 1.0f));
            g.setColour(MensisLook::fg.withAlpha(0.3f + 0.7f * d.env));
            if (d.backwards) g.drawEllipse(x - r, y - r, 2.0f * r, 2.0f * r, 1.0f);
            else g.fillEllipse(x - r, y - r, 2.0f * r, 2.0f * r);
        }
    }

    enum class Grab { None, Start, End, New };

    Grab grabAt(float px) const {
        const auto [st, en] = region();
        const float ds = std::abs(px - toX(st)), de = std::abs(px - toX(en));
        if (std::min(ds, de) <= 6.0f) return ds < de ? Grab::Start : Grab::End; // 6 px grab zone per edge
        return Grab::New;
    }

    // Region as 0..1 of the sample -> Start and Length params (Length = share of the whole sample).
    void apply(float st, float en) {
        st = std::clamp(st, 0.0f, 0.99f);
        set("start", st);
        set("length", std::clamp(en - st, 0.01f, 1.0f));
    }

    float xNorm(const juce::MouseEvent& e) const { return fromX(e.position.x); }

    // Overview of the sample in blocks of 256: loudest sample and sum of squares, built once per sample,
    // so drawing any zoom level stays fast even for long samples.
    static constexpr int kBlock = 256;
    void buildMip(const mensis::Sample& sample) {
        mipFor = &sample;
        const int n = sample.audio.getNumSamples(), chs = sample.audio.getNumChannels();
        const auto blocks = static_cast<size_t>((n + kBlock - 1) / kBlock);
        blockPeak.assign(blocks, 0.0f);
        blockSq.assign(blocks, 0.0);
        for (int ch = 0; ch < chs; ++ch) {
            const float* d = sample.audio.getReadPointer(ch);
            for (int i = 0; i < n; ++i) {
                const auto k = static_cast<size_t>(i / kBlock);
                blockPeak[k] = std::max(blockPeak[k], std::abs(d[i]));
                blockSq[k] += static_cast<double>(d[i]) * d[i] / chs;
            }
        }
        colsV0 = -1.0; // force new columns
    }

    void buildColumns(const mensis::Sample& sample) {
        colsV0 = v0;
        colsV1 = v1;
        cols.assign(static_cast<size_t>(std::max(0, getWidth())), {});
        const int n = sample.audio.getNumSamples(), chs = sample.audio.getNumChannels();
        for (size_t x = 0; x < cols.size(); ++x) {
            const int a = static_cast<int>(fromX(static_cast<float>(x)) * (n - 1));
            const int z = std::max(a + 1, static_cast<int>(fromX(static_cast<float>(x + 1)) * (n - 1)));
            float peak = 0.0f;
            double sq = 0.0;
            if (z - a >= 2 * kBlock) { // wide column: use the blocks
                for (int k = a / kBlock; k <= std::min((z - 1) / kBlock, static_cast<int>(blockPeak.size()) - 1); ++k) {
                    peak = std::max(peak, blockPeak[static_cast<size_t>(k)]);
                    sq += blockSq[static_cast<size_t>(k)];
                }
                sq /= ((z - 1) / kBlock - a / kBlock + 1) * kBlock;
            } else {
                for (int i = a; i < std::min(z, n); ++i)
                    for (int ch = 0; ch < chs; ++ch) {
                        const float v = sample.audio.getSample(ch, i);
                        peak = std::max(peak, std::abs(v));
                        sq += static_cast<double>(v) * v / chs;
                    }
                sq /= std::max(1, std::min(z, n) - a);
            }
            cols[x] = {peak, std::min(peak, static_cast<float>(std::sqrt(sq)))};
        }
    }

    struct Column { float peak = 0.0f, rms = 0.0f; };
    MensisProcessor& proc;
    size_t track;
    double v0 = 0.0, v1 = 1.0;           // zoom view
    const mensis::Sample* mipFor = nullptr;
    std::vector<float> blockPeak;
    std::vector<double> blockSq;
    std::vector<Column> cols;
    double colsV0 = -1.0, colsV1 = -1.0;
    Grab grab = Grab::None, hover = Grab::None;
    float downStart = 0.0f, downEnd = 1.0f, downX = 0.0f;
};

// Play button: white box with a black triangle when the track plays, dim outline when it's off,
// blinking while it waits for the next bar to switch.
class PlayButton final : public juce::Button {
public:
    PlayButton() : juce::Button("Play") { setClickingTogglesState(true); }
    bool waiting = false;

    void paintButton(juce::Graphics& g, bool over, bool) override {
        const auto box = getLocalBounds().toFloat().reduced(0.5f);
        const bool on = waiting ? juce::Time::getMillisecondCounter() / 250 % 2 == 0 : getToggleState();
        g.setColour(on ? MensisLook::fg : over ? MensisLook::fg.withAlpha(0.12f) : MensisLook::bg);
        g.fillRect(box);
        g.setColour(on ? MensisLook::fg : MensisLook::mid);
        g.drawRect(box, 1.0f);
        const auto c = box.getCentre();
        const float s = std::min(box.getWidth(), box.getHeight()) * 0.22f;
        juce::Path tri;
        tri.addTriangle(c.x - s * 0.8f, c.y - s, c.x - s * 0.8f, c.y + s, c.x + s, c.y);
        g.setColour(on ? MensisLook::bg : MensisLook::mid);
        g.fillPath(tri);
    }
};

// One track: number, Load button, file name; waveform on the left, knobs and switches on the right.
inline juce::String barsText(double bars) {
    if (bars < 1.0) return (bars < 0.375 ? "1/4" : "1/2") + juce::String(" BAR");
    return juce::String(juce::roundToInt(bars)) + (bars > 1.0 ? " BARS" : " BAR");
}

class TrackStrip final : public juce::Component {
public:
    static constexpr int kKnobAreaWidth = 9 * 60; // knob columns + toggle column, right of the waveform
    std::function<void()> onLoad;

    TrackStrip(MensisProcessor& p, size_t t) : proc(p), track(t), wave(p, t) {
        // Track number = on/off button (like Live's track activator): lit = playing.
        number.setText(juce::String(t + 1), juce::dontSendNotification);
        number.setFont(MensisLook::font(22.0f, true));
        play.setTooltip("Play / stop this track (switches on the next bar)");
        playAttachment = std::make_unique<juce::ButtonParameterAttachment>(*p.params.getParameter(id("on")), play);
        addAndMakeVisible(number);
        addAndMakeVisible(play);
        attachToggle(mute, "mute", "M");
        attachToggle(solo, "solo", "S");
        mute.button.setTooltip("Mute (instant)");
        solo.button.setTooltip("Solo: only soloed tracks play");
        name.setFont(MensisLook::font(12.0f));
        name.setColour(juce::Label::textColourId, MensisLook::mid);
        load.setButtonText("LOAD");
        load.onClick = [this] { if (onLoad) onLoad(); };
        remove.setButtonText(juce::CharPointer_UTF8("\xe2\x9c\x95")); // ✕
        remove.setTooltip("Remove sample");
        remove.onClick = [this] { proc.clearSample(track); refresh(); };
        for (auto* c : std::initializer_list<juce::Component*>{&name, &load, &remove, &wave}) addAndMakeVisible(c);

        const std::array<std::pair<const char*, const char*>, 7> knobIds{{{"start", "START"}, {"length", "LENGTH"},
            {"bars", "BARS"}, {"pitch", "PITCH"}, {"level", "LEVEL"}, {"pan", "PAN"}, {"chaos", "CHAOS"}}};
        for (size_t i = 0; i < knobs.size(); ++i) {
            knobs[i].attach(*p.params.getParameter(id(knobIds[i].first)), knobIds[i].second);
            addAndMakeVisible(knobs[i]);
        }
        const std::array<std::pair<const char*, const char*>, 4> toggleIds{
            {{"reverse", "REVERSE"}, {"warp", "WARP"}, {"slice", "SLICE"}, {"beat", "BEAT"}}};
        toggles[3].button.setTooltip("BEAT: Chaos shuffles hits and adds rolls, always on the grid (needs SLICE)");
        for (size_t i = 0; i < toggles.size(); ++i) attachToggle(toggles[i], toggleIds[i].first, toggleIds[i].second);

        const std::array<std::pair<const char*, const char*>, 7> grainIds{{{"grain.mix", "MIX"}, {"grain.size", "SIZE"},
            {"grain.density", "DENSITY"}, {"grain.spray", "SPRAY"}, {"grain.detune", "DETUNE"},
            {"grain.reverse", "REVERSE"}, {"grain.width", "WIDTH"}}};
        for (size_t i = 0; i < grainKnobs.size(); ++i) {
            grainKnobs[i].attach(*p.params.getParameter(id(grainIds[i].first)), grainIds[i].second);
            addChildComponent(grainKnobs[i]);
        }
        attachToggle(freeze, "grain.freeze", "FREEZE");
        freeze.button.setVisible(false);

        const std::array<std::pair<const char*, const char*>, 8> resIds{{{"res.mix", "MIX"}, {"res.root", "ROOT"},
            {"res.chord", "CHORD"}, {"res.decay", "DECAY"}, {"res.color", "COLOR"}, {"flt.cutoff", "CUTOFF"},
            {"flt.reso", "RESO"}, {"flt.morph", "MORPH"}}};
        for (size_t i = 0; i < resKnobs.size(); ++i) {
            resKnobs[i].attach(*p.params.getParameter(id(resIds[i].first)), resIds[i].second);
            addChildComponent(resKnobs[i]);
        }
        const std::array<std::pair<const char*, const char*>, 8> colorIds{{{"col.drive", "DRIVE"}, {"col.char", "CHARACTER"},
            {"col.bits", "BITS"}, {"col.rate", "RATE"}, {"col.squash", "SQUASH"}, {"col.tilt", "TILT"},
            {"col.out", "OUTPUT"}, {"col.mix", "MIX"}}};
        for (size_t i = 0; i < colorKnobs.size(); ++i) {
            colorKnobs[i].attach(*p.params.getParameter(id(colorIds[i].first)), colorIds[i].second);
            addChildComponent(colorKnobs[i]);
        }
        const std::array<std::pair<const char*, const char*>, 4> eqIds{{{"eq.low", "LOW"}, {"eq.mid", "MID"},
            {"eq.midfreq", "FREQ"}, {"eq.high", "HIGH"}}};
        for (size_t i = 0; i < eqKnobs.size(); ++i) {
            eqKnobs[i].attach(*p.params.getParameter(id(eqIds[i].first)), eqIds[i].second);
            addChildComponent(eqKnobs[i]);
        }
    }

    // 0 = LOOP knobs, 1 = GRAINS knobs, 2 = RESONATOR, 3 = COLOR, 4 = filter + EQ. The waveform and track header stay.
    void setPage(int newPage) {
        page = newPage;
        repaint(); // group labels
        for (size_t i = 0; i < resKnobs.size(); ++i) resKnobs[i].setVisible(page == (i < 5 ? 2 : 4)); // the last 3 are the filter
        for (auto& k : eqKnobs) k.setVisible(page == 4);
        for (auto& k : colorKnobs) k.setVisible(page == 3);
        for (auto& k : knobs) k.setVisible(page == 0);
        for (auto& t : toggles) t.button.setVisible(page == 0);
        for (auto& k : grainKnobs) k.setVisible(page == 1);
        freeze.button.setVisible(page == 1);
    }

    void refresh() {
        const auto file = proc.getSampleFile(track);
        const bool loaded = file != juce::File{};
        const auto missing = proc.getMissingSample(track);
        name.setText(loaded ? file.getFileNameWithoutExtension() + "   " + barsText(proc.getBars(track))
                     : missing.isNotEmpty() ? "MISSING: " + juce::File(missing).getFileName() + "  (LOAD to find it)"
                                            : juce::String("empty"),
                     juce::dontSendNotification);
        name.setColour(juce::Label::textColourId, missing.isNotEmpty() ? MensisLook::fg : MensisLook::mid);
        const float alpha = loaded ? 1.0f : 0.4f; // empty track: knobs dimmed (still usable, for setting up first)
        for (auto& k : knobs) k.setAlpha(alpha);
        for (auto& k : grainKnobs) k.setAlpha(alpha);
        for (auto& k : resKnobs) k.setAlpha(alpha);
        for (auto& k : colorKnobs) k.setAlpha(alpha);
        for (auto& k : eqKnobs) k.setAlpha(alpha);
        for (auto& t : toggles) t.button.setAlpha(alpha);
        freeze.button.setAlpha(alpha);
        knobs[3].setEnabled(toggles[1].button.getToggleState()); // Pitch only works with Warp on
        toggles[3].button.setEnabled(toggles[2].button.getToggleState()); // BEAT needs SLICE
        remove.setEnabled(loaded);
        const bool waiting = play.getToggleState() != proc.isTrackOn(track);
        if (waiting || waiting != play.waiting) play.repaint();
        play.waiting = waiting;
        const bool audible = proc.isTrackAudible(track);
        wave.setAlpha(audible ? 1.0f : 0.3f); // silent tracks fade back
        number.setColour(juce::Label::textColourId, audible ? MensisLook::fg : MensisLook::mid);
        wave.repaint();
    }

    void paint(juce::Graphics& g) override {
        g.setColour(MensisLook::dim);
        g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
        if (page < 0 || page >= kPages) return;
        for (const auto& [label, r] : headers[static_cast<size_t>(page)]) { // tiny group name over a thin line
            g.setColour(MensisLook::mid);
            g.setFont(MensisLook::font(9.0f, true));
            g.drawText(label, r.withTrimmedBottom(4), juce::Justification::bottomLeft);
            g.setColour(MensisLook::dim);
            g.fillRect(r.getX(), r.getBottom() - 2, r.getWidth(), 1);
        }
    }

    void resized() override {
        auto area = getLocalBounds().reduced(0, 10);
        auto head = area.removeFromTop(24);
        number.setBounds(head.removeFromLeft(26));
        play.setBounds(head.removeFromLeft(28).reduced(2, 1));
        mute.button.setBounds(head.removeFromLeft(26).reduced(1, 1));
        solo.button.setBounds(head.removeFromLeft(26).reduced(1, 1));
        head.removeFromLeft(8);
        load.setBounds(head.removeFromLeft(56).reduced(0, 2));
        head.removeFromLeft(4);
        remove.setBounds(head.removeFromLeft(24).reduced(0, 2));
        name.setBounds(head.withTrimmedLeft(8));
        area.removeFromTop(6);

        auto right = area.removeFromRight(kKnobAreaWidth);
        wave.setBounds(area.withTrimmedRight(16));

        // Each page's controls in small groups: a label + line on top, knobs below, switches in a column.
        const auto block = right.withSizeKeepingCentre(right.getWidth(), std::min(right.getHeight(), kHeaderH + 96));
        for (int pg = 0; pg < kPages; ++pg) {
            auto& hs = headers[static_cast<size_t>(pg)];
            hs.clear();
            int x = block.getX();
            for (const auto& group : groupsFor(pg)) {
                const int w = static_cast<int>(group.knobs.size()) * kKnobW + (group.buttons.empty() ? 0 : kButtonW);
                hs.push_back({group.name, {x, block.getY(), w, kHeaderH}});
                auto row = juce::Rectangle<int>(x, block.getY() + kHeaderH, w, block.getHeight() - kHeaderH);
                for (auto* k : group.knobs) k->setBounds(row.removeFromLeft(kKnobW));
                if (!group.buttons.empty()) {
                    const int stackH = static_cast<int>(group.buttons.size()) * 26 - 6;
                    auto stack = row.withSizeKeepingCentre(row.getWidth(), stackH).translated(0, 4).reduced(4, 0);
                    for (auto* b : group.buttons) {
                        b->setBounds(stack.removeFromTop(20));
                        stack.removeFromTop(6);
                    }
                }
                x += w + kGap;
            }
        }
    }

private:
    static constexpr int kPages = 5, kKnobW = 54, kButtonW = 64, kGap = 10, kHeaderH = 16;

    struct Group { const char* name; std::vector<juce::Component*> knobs, buttons; };
    std::vector<Group> groupsFor(int pg) {
        switch (pg) {
            case 0: return {{"REGION", {&knobs[0], &knobs[1]}, {}},
                            {"SPEED", {&knobs[2], &knobs[3]}, {&toggles[0].button, &toggles[1].button}},
                            {"", {&knobs[6]}, {&toggles[2].button, &toggles[3].button}},
                            {"OUT", {&knobs[4], &knobs[5]}, {}}};
            case 1: return {{"GRAIN", {&grainKnobs[0], &grainKnobs[1], &grainKnobs[2]}, {&freeze.button}},
                            {"SCATTER", {&grainKnobs[3], &grainKnobs[4], &grainKnobs[5], &grainKnobs[6]}, {}}};
            case 2: return {{"RESONATOR", {&resKnobs[0], &resKnobs[1], &resKnobs[2], &resKnobs[3], &resKnobs[4]}, {}}};
            case 3: return {{"SATURATE", {&colorKnobs[0], &colorKnobs[1]}, {}},
                            {"CRUSH", {&colorKnobs[2], &colorKnobs[3]}, {}},
                            {"SHAPE", {&colorKnobs[4], &colorKnobs[5]}, {}},
                            {"OUT", {&colorKnobs[6], &colorKnobs[7]}, {}}};
            case 4: return {{"FILTER", {&resKnobs[5], &resKnobs[6], &resKnobs[7]}, {}},
                            {"EQ", {&eqKnobs[0], &eqKnobs[1], &eqKnobs[2], &eqKnobs[3]}, {}}};
            default: return {};
        }
    }
    std::array<std::vector<std::pair<juce::String, juce::Rectangle<int>>>, kPages> headers;
    int page = 0;

    juce::String id(const char* name) const { return "t" + juce::String(track + 1) + "." + name; }

    struct Toggle { juce::TextButton button; std::unique_ptr<juce::ButtonParameterAttachment> attachment; };
    void attachToggle(Toggle& t, const char* paramName, const char* text) {
        t.button.setButtonText(text);
        t.button.setClickingTogglesState(true);
        t.attachment = std::make_unique<juce::ButtonParameterAttachment>(*proc.params.getParameter(id(paramName)), t.button);
        addAndMakeVisible(t.button);
    }
    MensisProcessor& proc;
    size_t track;
    juce::Label name;
    juce::TextButton load, remove;
    WaveformView wave;
    std::array<Knob, 7> knobs;
    std::array<Toggle, 4> toggles;
    std::array<Knob, 7> grainKnobs;
    std::array<Knob, 8> resKnobs, colorKnobs; // resKnobs: 5 resonator + 3 filter (shown on the EQ page)
    std::array<Knob, 4> eqKnobs;
    Toggle freeze, mute, solo;
    juce::Label number;
    PlayButton play;
    std::unique_ptr<juce::ButtonParameterAttachment> playAttachment;
};

// SPACE page: one SEND per track (lined up with its strip), lines carrying each send into a shared bus
// and on into the Space's knobs. Line brightness = send amount.
class SpacePanel final : public juce::Component, private juce::Timer {
public:
    explicit SpacePanel(MensisProcessor& p) {
        for (size_t t = 0; t < sends.size(); ++t) {
            sends[t].attach(*p.params.getParameter("t" + juce::String(t + 1) + ".send"), "SEND");
            addAndMakeVisible(sends[t]);
            sendValues[t] = p.params.getRawParameterValue("t" + juce::String(t + 1) + ".send");
        }
        const std::array<std::pair<const char*, const char*>, 6> ids{{{"space.time", "TIME"}, {"space.feedback", "FEEDBACK"},
            {"space.blend", "BLEND"}, {"space.size", "SIZE"}, {"space.tone", "TONE"}, {"space.return", "RETURN"}}};
        for (size_t i = 0; i < knobs.size(); ++i) {
            knobs[i].attach(*p.params.getParameter(ids[i].first), ids[i].second);
            addAndMakeVisible(knobs[i]);
        }
        freeze.setButtonText("FREEZE");
        freeze.setClickingTogglesState(true);
        freeze.setTooltip("Hold whatever is ringing in the Space, forever");
        freezeAttachment = std::make_unique<juce::ButtonParameterAttachment>(*p.params.getParameter("space.freeze"), freeze);
        addAndMakeVisible(freeze);
    }

    // Where each track strip sits, in this panel's coordinates, so its SEND lines up with it.
    void setRows(const std::array<juce::Rectangle<int>, MensisProcessor::kNumTracks>& r) { rows = r; resized(); }

    void visibilityChanged() override {
        if (isVisible()) startTimerHz(15);
        else stopTimer();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(MensisLook::bg); // covers the track strips' divider lines underneath
        const auto block = knobBlock();
        auto title = block.translated(0, -40).withHeight(28);
        g.setColour(MensisLook::fg);
        g.setFont(MensisLook::font(13.0f, true));
        g.drawText("S P A C E", title, juce::Justification::bottomLeft);
        g.setColour(MensisLook::mid);
        g.setFont(MensisLook::font(10.0f));
        g.drawText("SHARED DELAY + REVERB", title, juce::Justification::bottomRight);

        // Send lines -> bus -> into the Space.
        const float merge = static_cast<float>(block.getCentreY());
        float loudest = 0.0f, top = merge, bottom = merge;
        for (size_t t = 0; t < sends.size(); ++t) {
            const float amount = sendValues[t]->load(), y = sendY(t);
            loudest = std::max(loudest, amount);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
            g.setColour(MensisLook::fg.withAlpha(0.12f + 0.88f * amount));
            g.fillRect(lineStart(t), y - 0.75f, busX() - lineStart(t), 1.5f);
        }
        g.setColour(MensisLook::fg.withAlpha(0.12f + 0.88f * loudest));
        g.fillRect(busX() - 0.75f, top, 1.5f, bottom - top);
        g.fillRect(busX(), merge - 0.75f, intoX() - busX(), 1.5f);
        juce::Path arrow;
        arrow.addTriangle(intoX(), merge - 4.0f, intoX(), merge + 4.0f, intoX() + 6.0f, merge);
        g.fillPath(arrow);
    }

    void resized() override {
        for (size_t t = 0; t < sends.size(); ++t)
            sends[t].setBounds(rows[t].withX(0).withWidth(kSendWidth).withSizeKeepingCentre(kSendWidth, std::min(rows[t].getHeight(), 96)));
        auto block = knobBlock();
        auto top = block.removeFromTop(kRowH), bottom = block.removeFromTop(kRowH);
        const int w = top.getWidth() / 4;
        for (size_t i = 0; i < 3; ++i) knobs[i].setBounds(top.removeFromLeft(w).withSizeKeepingCentre(64, 96));
        for (size_t i = 3; i < 6; ++i) knobs[i].setBounds(bottom.removeFromLeft(w).withSizeKeepingCentre(64, 96));
        freeze.setBounds(bottom.removeFromLeft(w).withSizeKeepingCentre(80, 24));
    }

private:
    static constexpr int kSendWidth = 64, kRowH = 110;

    // Two rows of knobs, centred in the panel, right of the bus.
    juce::Rectangle<int> knobBlock() const {
        const auto r = getLocalBounds().withTrimmedLeft(kSendWidth + 48);
        return r.withSizeKeepingCentre(r.getWidth(), 2 * kRowH);
    }
    float busX() const { return static_cast<float>(kSendWidth + 18); }
    float intoX() const { return static_cast<float>(knobBlock().getX() - 10); }
    float lineStart(size_t t) const { return static_cast<float>(sends[t].getRight()) - 4.0f; }
    float sendY(size_t t) const { return static_cast<float>(sends[t].getBounds().getCentreY()) + 2.0f; } // the knob, not its label

    void timerCallback() override { repaint(); } // line brightness follows the sends (knobs and automation)

    std::array<Knob, MensisProcessor::kNumTracks> sends;
    std::array<std::atomic<float>*, MensisProcessor::kNumTracks> sendValues{};
    std::array<Knob, 6> knobs;
    juce::TextButton freeze;
    std::unique_ptr<juce::ButtonParameterAttachment> freezeAttachment;
    std::array<juce::Rectangle<int>, MensisProcessor::kNumTracks> rows{};
};

// MOD page: the 4 fixed macros on top, then the 4 mod slots, each with a live scope of its signal.
class ModPanel final : public juce::Component, private juce::Timer {
public:
    std::function<void(int)> onMap; // MAP pressed on a slot

    explicit ModPanel(MensisProcessor& p) : proc(p) {
        const std::array<std::pair<const char*, const char*>, 4> macroIds{
            {{"macro.destroy", "DESTROY"}, {"macro.drift", "DRIFT"}, {"macro.ring", "RING"}, {"macro.space", "SPACE"}}};
        for (size_t i = 0; i < macros.size(); ++i) {
            macros[i].attach(*p.params.getParameter(macroIds[i].first), macroIds[i].second);
            addAndMakeVisible(macros[i]);
        }
        for (size_t m = 0; m < rows.size(); ++m) {
            auto& row = rows[m];
            const auto prefix = "mod" + juce::String(m + 1) + ".";
            const std::array<std::pair<const char*, const char*>, 4> ids{{{"type", "TYPE"}, {"rate", "RATE"}, {"shape", "SHAPE"}, {"depth", "DEPTH"}}};
            for (size_t k = 0; k < row.knobs.size(); ++k) {
                row.knobs[k].attach(*p.params.getParameter(prefix + ids[k].first), ids[k].second);
                addAndMakeVisible(row.knobs[k]);
            }
            row.hz.attach(*p.params.getParameter(prefix + "hz"), "RATE");
            addChildComponent(row.hz);
            row.sync.setButtonText("SYNC");
            row.sync.setClickingTogglesState(true);
            row.sync.setTooltip("On: RATE in note lengths, locked to Live. Off: RATE in Hz");
            row.syncAttachment = std::make_unique<juce::ButtonParameterAttachment>(*p.params.getParameter(prefix + "sync"), row.sync);
            addAndMakeVisible(row.sync);
            row.syncParam = p.params.getRawParameterValue(prefix + "sync");
            row.typeParam = p.params.getRawParameterValue(prefix + "type");
            row.followParam = p.params.getParameter(prefix + "follow");
            row.follow.setTooltip("What the Env listens to. Another track = sidechain (e.g. T4 drums ducking T1)");
            row.follow.onClick = [&row] { // click = next choice: SELF, T1..T4, MIX
                auto* f = row.followParam;
                const int n = f->getNumSteps();
                const int next = (juce::roundToInt(f->getValue() * static_cast<float>(n - 1)) + 1) % n;
                f->beginChangeGesture();
                f->setValueNotifyingHost(static_cast<float>(next) / static_cast<float>(n - 1));
                f->endChangeGesture();
            };
            addChildComponent(row.follow);
            row.map.setButtonText("MAP");
            row.map.setTooltip("Press, then touch any knob: it becomes this modulator's target");
            row.map.onClick = [this, m] { if (onMap) onMap(static_cast<int>(m)); };
            row.clear.setButtonText(juce::CharPointer_UTF8("\xe2\x9c\x95")); // ✕
            row.clear.setTooltip("Remove the target");
            row.clear.onClick = [this, m] { proc.setModTarget(m, {}); };
            row.target.setFont(MensisLook::font(11.0f, true));
            row.target.setJustificationType(juce::Justification::centredLeft);
            for (auto* c : std::initializer_list<juce::Component*>{&row.map, &row.clear, &row.target}) addAndMakeVisible(c);
        }
        startTimerHz(30);
    }

    void setArmed(int slot) {
        armed = slot;
        for (size_t m = 0; m < rows.size(); ++m) rows[m].map.setToggleState(static_cast<int>(m) == armed, juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(MensisLook::bg); // covers the track strips' divider lines underneath
        auto heading = [&](const char* text, juce::Rectangle<int> r) {
            g.setColour(MensisLook::fg);
            g.setFont(MensisLook::font(13.0f, true));
            g.drawText(text, r, juce::Justification::bottomLeft);
        };
        heading("M A C R O S", {kLeft, 0, getWidth() - kLeft, 28});
        heading("M O D U L A T I O N", {kLeft, kMacroH + 6, getWidth() - kLeft, 28});
        g.setColour(MensisLook::dim);
        g.fillRect(static_cast<float>(kLeft), static_cast<float>(kMacroH), static_cast<float>(getWidth() - kLeft), 1.0f);

        for (size_t m = 0; m < rows.size(); ++m) {
            const auto row = rowArea(m);
            g.setColour(MensisLook::fg);
            g.setFont(MensisLook::font(20.0f, true));
            g.drawText(juce::String(m + 1), row.withWidth(24).toFloat(), juce::Justification::centred);
            drawScope(g, m, scopeArea(m).toFloat());
        }
    }

    void resized() override {
        auto top = juce::Rectangle<int>(kLeft, 30, getWidth() - kLeft, kMacroH - 34);
        const int w = top.getWidth() / static_cast<int>(macros.size());
        for (auto& k : macros) k.setBounds(top.removeFromLeft(w).withSizeKeepingCentre(72, 96));
        for (size_t m = 0; m < rows.size(); ++m) {
            auto row = rowArea(m).withTrimmedLeft(28);
            for (auto& k : rows[m].knobs) k.setBounds(row.removeFromLeft(58).withSizeKeepingCentre(58, std::min(row.getHeight(), 92)));
            rows[m].hz.setBounds(rows[m].knobs[1].getBounds()); // same spot: one shows, by SYNC
            rows[m].sync.setBounds(rows[m].knobs[1].getBounds().withY(rows[m].knobs[1].getBottom() + 2).withHeight(18).reduced(6, 0));
            rows[m].follow.setBounds(rows[m].knobs[0].getBounds().withY(rows[m].knobs[0].getBottom() + 2).withHeight(18).reduced(2, 0));
            auto buttons = row.removeFromLeft(56).withSizeKeepingCentre(48, 50);
            rows[m].map.setBounds(buttons.removeFromTop(22));
            buttons.removeFromTop(6);
            rows[m].clear.setBounds(buttons.removeFromTop(22));
            rows[m].target.setBounds(scopeArea(m).translated(0, scopeArea(m).getHeight() + 2).withHeight(18));
        }
    }

private:
    static constexpr int kLeft = 8, kMacroH = 140;
    juce::Rectangle<int> rowArea(size_t m) const {
        const int h = (getHeight() - kMacroH - 40) / static_cast<int>(rows.size());
        return {kLeft, kMacroH + 40 + static_cast<int>(m) * h, getWidth() - kLeft, h};
    }
    juce::Rectangle<int> scopeArea(size_t m) const {
        auto r = rowArea(m).withTrimmedLeft(28 + 4 * 58 + 56 + 8);
        return r.withSizeKeepingCentre(r.getWidth(), 44).translated(0, -10);
    }

    // The last ~3 s of the modulator's signal as a thin line; bipolar around the middle, Env from the bottom.
    void drawScope(juce::Graphics& g, size_t m, juce::Rectangle<float> r) const {
        const bool env = proc.params.getRawParameterValue("mod" + juce::String(m + 1) + ".type")->load() > 1.5f;
        g.setColour(MensisLook::dim);
        g.drawRect(r, 1.0f);
        if (!env) g.drawHorizontalLine(static_cast<int>(r.getCentreY()), r.getX(), r.getRight());
        juce::Path line;
        const auto& hist = history[m];
        for (size_t i = 0; i < hist.size(); ++i) {
            const float v = hist[(histPos + i) % hist.size()];
            const float y = env ? r.getBottom() - 2.0f - v * (r.getHeight() - 4.0f) : r.getCentreY() - v * (r.getHeight() * 0.5f - 2.0f);
            const float x = r.getX() + r.getWidth() * static_cast<float>(i) / static_cast<float>(hist.size() - 1);
            if (i == 0) line.startNewSubPath(x, y);
            else line.lineTo(x, y);
        }
        const bool active = proc.getModTarget(m).isNotEmpty();
        g.setColour(active ? MensisLook::fg : MensisLook::mid);
        g.strokePath(line, juce::PathStrokeType(1.5f));
    }

    void timerCallback() override {
        histPos = (histPos + 1) % history[0].size();
        for (size_t m = 0; m < rows.size(); ++m) {
            history[m][(histPos + history[m].size() - 1) % history[m].size()] = proc.getModSignal(m);
            const auto id = proc.getModTarget(m);
            juce::String text = static_cast<int>(m) == armed ? "TOUCH A KNOB..."
                                : id.isEmpty() ? "NO TARGET" : proc.params.getParameter(id)->getName(40).toUpperCase();
            rows[m].target.setText(text, juce::dontSendNotification);
            rows[m].target.setColour(juce::Label::textColourId, id.isEmpty() && static_cast<int>(m) != armed ? MensisLook::mid : MensisLook::fg);
            rows[m].clear.setEnabled(id.isNotEmpty());
            const bool synced = rows[m].syncParam->load() > 0.5f;
            if (rows[m].knobs[1].isVisible() != synced) {
                rows[m].knobs[1].setVisible(synced);
                rows[m].hz.setVisible(!synced);
            }
            rows[m].follow.setVisible(rows[m].typeParam->load() > 1.5f); // Env only
            rows[m].follow.setButtonText(rows[m].followParam->getCurrentValueAsText().toUpperCase());
        }
        if (isShowing()) repaint();
    }

    struct Row {
        std::array<Knob, 4> knobs; // TYPE, RATE (note lengths), SHAPE, DEPTH
        Knob hz;                   // RATE in Hz, shown instead when SYNC is off
        juce::TextButton map, clear, sync, follow; // FOLLOW: Env source, shown only for Env
        std::atomic<float>* typeParam = nullptr;
        juce::RangedAudioParameter* followParam = nullptr;
        std::unique_ptr<juce::ButtonParameterAttachment> syncAttachment;
        std::atomic<float>* syncParam = nullptr;
        juce::Label target;
    };
    MensisProcessor& proc;
    std::array<Knob, 4> macros;
    std::array<Row, MensisProcessor::kModSlots> rows;
    std::array<std::array<float, 90>, MensisProcessor::kModSlots> history{};
    size_t histPos = 0;
    int armed = -1;
};

// Scene bar: 8 snapshot slots. Click = recall on the next bar (an empty slot: store), SAVE+click or
// Shift-click = store, Alt-click = clear. Dim = empty, outline = stored, filled = current,
// outline + dot = current but a knob has moved since, blinking = waiting for the bar.
class SceneBar final : public juce::Component, private juce::Timer {
public:
    explicit SceneBar(MensisProcessor& p) : proc(p) {
        save.setButtonText("SAVE");
        save.setClickingTogglesState(true);
        save.setTooltip("Then click a slot to store the current sound in it");
        addAndMakeVisible(save);
        startTimerHz(8);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(MensisLook::dim);
        g.fillRect(0.0f, 0.0f, static_cast<float>(getWidth()), 1.0f);
        g.setColour(MensisLook::fg);
        g.setFont(MensisLook::font(13.0f, true));
        g.drawText("S C E N E S", juce::Rectangle<int>(0, 0, kLabel, getHeight()), juce::Justification::centredLeft);
        const int current = proc.getCurrentScene(), pending = proc.getPendingScene();
        const bool changed = proc.sceneChanged();
        for (int s = 0; s < MensisProcessor::kScenes; ++s) {
            const auto r = slotBounds(s).toFloat();
            const bool stored = proc.hasScene(s), filled = (s == current && !changed) || (s == pending && blink);
            g.setColour(filled ? MensisLook::fg : MensisLook::bg);
            g.fillRect(r);
            g.setColour(stored || s == pending ? MensisLook::fg : MensisLook::dim);
            g.drawRect(r, 1.0f);
            g.setColour(filled ? MensisLook::bg : stored ? MensisLook::fg : MensisLook::mid);
            g.setFont(MensisLook::font(12.0f, true));
            g.drawText(juce::String(s + 1), r, juce::Justification::centred);
            if (s == current && changed && !filled) g.fillRect(r.getRight() - 8.0f, r.getY() + 4.0f, 4.0f, 4.0f);
        }
        g.setColour(MensisLook::mid);
        g.setFont(MensisLook::font(10.0f));
        g.drawText(save.getToggleState() ? "CLICK A SLOT TO SAVE INTO IT"
                                         : "CLICK: PLAY ON NEXT BAR, OR SAVE IF EMPTY   SHIFT: SAVE   ALT: CLEAR",
                   getLocalBounds().withTrimmedLeft(save.getRight() + 16), juce::Justification::centredLeft);
    }

    void resized() override {
        save.setBounds(slotBounds(MensisProcessor::kScenes - 1).getRight() + 14, (getHeight() - 24) / 2, 60, 24);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        for (int s = 0; s < MensisProcessor::kScenes; ++s) {
            if (!slotBounds(s).contains(e.getPosition())) continue;
            if (e.mods.isAltDown() && !save.getToggleState()) {
                proc.clearScene(s);
            } else if (save.getToggleState() || e.mods.isShiftDown() || !proc.hasScene(s)) { // empty slot: a click stores
                proc.saveScene(s);
                save.setToggleState(false, juce::dontSendNotification);
            } else {
                proc.recallScene(s);
            }
            repaint();
        }
    }

private:
    static constexpr int kLabel = 110;
    juce::Rectangle<int> slotBounds(int s) const { return {kLabel + s * 46, (getHeight() - 26) / 2, 40, 26}; }
    void timerCallback() override {
        blink = !blink;
        repaint();
    }

    MensisProcessor& proc;
    juce::TextButton save;
    bool blink = false;
};

// The whole UI at its design size (kWidth x kHeight). The editor below scales it as one piece.
class MensisPanel final : public juce::Component, public juce::FileDragAndDropTarget,
                          public juce::TextDragAndDropTarget, public KnobHost, private juce::Timer {
public:
    static constexpr int kWidth = 1040, kHeight = 884;

    explicit MensisPanel(MensisProcessor& p) : proc(p) {
        setLookAndFeel(&look);
        for (size_t t = 0; t < MensisProcessor::kNumTracks; ++t) {
            strips[t] = std::make_unique<TrackStrip>(p, t);
            strips[t]->onLoad = [this, t] { chooseFile(t); };
            addAndMakeVisible(*strips[t]);
        }
        const std::array<std::pair<const char*, const char*>, 5> globalIds{
            {{"chaos", "CHAOS"}, {"chaos.grid", "GRID"}, {"chaos.feel", "FEEL"}, {"swing", "SWING"}, {"seed", "SEED"}}};
        for (size_t i = 0; i < globalKnobs.size(); ++i) {
            globalKnobs[i].attach(*p.params.getParameter(globalIds[i].first), globalIds[i].second);
            addAndMakeVisible(globalKnobs[i]);
        }
        volume.attach(*p.params.getParameter("master"), "VOLUME");
        addAndMakeVisible(volume);

        const std::array<const char*, 7> pageNames{"LOOP", "GRAINS", "RESO", "COLOR", "EQ", "SPACE", "MOD"};
        for (int i = 0; i < 7; ++i) {
            auto& b = pages[static_cast<size_t>(i)];
            b.setButtonText(pageNames[static_cast<size_t>(i)]);
            b.setRadioGroupId(1);
            b.setClickingTogglesState(true);
            b.onClick = [this, i] {
                for (auto& s : strips) s->setPage(i);
                spacePanel.setVisible(i == 5);
                modPanel.setVisible(i == 6);
            };
            addAndMakeVisible(b);
        }
        pages[0].setToggleState(true, juce::dontSendNotification);
        addChildComponent(spacePanel); // shown on the SPACE page
        addChildComponent(modPanel);   // shown on the MOD page
        addAndMakeVisible(sceneBar);
        modPanel.onMap = [this](int slot) { arm(mapArmed == slot ? -1 : slot); };

        sendLookAndFeelChange(); // the strips were built before they had a parent; restyle their knob value boxes
        setSize(kWidth, kHeight); // tall strips: room for a big waveform; scene bar at the bottom
        timerCallback();
        startTimerHz(30); // moves the playheads; also picks up samples restored from a saved Live set
    }

    ~MensisPanel() override { setLookAndFeel(nullptr); }

    std::optional<float> modulatedValue(const juce::RangedAudioParameter& param) override {
        return proc.getModulatedValue(param);
    }
    void knobTouched(juce::RangedAudioParameter& param) override { // MAP: the next knob you touch
        if (mapArmed < 0 || param.getParameterID().startsWith("mod")) return;
        proc.setModTarget(static_cast<size_t>(mapArmed), param.getParameterID());
        // A fresh mapping should do something right away: DEPTH 0 -> +50%.
        auto* depth = proc.params.getParameter("mod" + juce::String(mapArmed + 1) + ".depth");
        if (depth->convertFrom0to1(depth->getValue()) == 0.0f) {
            depth->beginChangeGesture();
            depth->setValueNotifyingHost(depth->convertTo0to1(0.5f));
            depth->endChangeGesture();
        }
        arm(-1);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(MensisLook::bg);
        auto head = getLocalBounds().reduced(20, 0).removeFromTop(kHeader);
        g.setColour(MensisLook::fg);
        g.setFont(MensisLook::font(16.0f, true));
        g.drawText("M E N S I S", head.removeFromTop(kHeader / 2 + 8), juce::Justification::bottomLeft);
        g.setColour(mapArmed >= 0 ? MensisLook::fg : MensisLook::mid);
        g.setFont(MensisLook::font(11.0f, mapArmed >= 0));
        g.drawText(mapArmed >= 0 ? "MOD " + juce::String(mapArmed + 1) + ": TOUCH A KNOB" : juce::String("SAMPLER"),
                   head, juce::Justification::topLeft);
    }

    void resized() override {
        auto head = getLocalBounds().reduced(20, 0).removeFromTop(kHeader).reduced(0, 12);
        volume.setBounds(head.removeFromRight(70)); // master, far right
        head.removeFromRight(20);
        for (size_t i = globalKnobs.size(); i-- > 0;) globalKnobs[i].setBounds(head.removeFromRight(i == 0 ? 76 : 60));
        auto tabs = head.withTrimmedLeft(130).removeFromLeft(7 * 62).withSizeKeepingCentre(7 * 62, 24);
        for (auto& b : pages) b.setBounds(tabs.removeFromLeft(62).withTrimmedRight(4));

        auto area = getLocalBounds().reduced(20, 0).withTrimmedTop(kHeader).withTrimmedBottom(12);
        sceneBar.setBounds(area.removeFromBottom(44));
        area.removeFromBottom(6);
        const int h = area.getHeight() / static_cast<int>(MensisProcessor::kNumTracks);
        const auto stripsArea = area;
        for (auto& s : strips) s->setBounds(area.removeFromTop(h));
        // SPACE page panel sits over the strips' knob area (the strips hide their knobs on that page).
        spacePanel.setBounds(stripsArea.withTrimmedLeft(stripsArea.getWidth() - TrackStrip::kKnobAreaWidth));
        std::array<juce::Rectangle<int>, MensisProcessor::kNumTracks> rows;
        for (size_t t = 0; t < rows.size(); ++t)
            rows[t] = strips[t]->getBounds().withTrimmedTop(40).translated(-spacePanel.getX(), -spacePanel.getY());
        spacePanel.setRows(rows);
        modPanel.setBounds(spacePanel.getBounds());
    }

    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }

    void filesDropped(const juce::StringArray& files, int, int y) override {
        size_t track = 0; // dropped outside the tracks: first empty track, else track 1
        for (size_t t = MensisProcessor::kNumTracks; t-- > 0;)
            if (proc.getSampleFile(t) == juce::File{}) track = t;
        for (size_t t = 0; t < MensisProcessor::kNumTracks; ++t)
            if (y >= strips[t]->getY() && y < strips[t]->getBottom()) track = t;
        load(track, juce::File(files[0]));
    }

    // Some apps drag a file as text (a path or a file:// URL) instead of as a file.
    bool isInterestedInTextDrag(const juce::String& text) override { return toFile(text) != juce::File{}; }
    void textDropped(const juce::String& text, int x, int y) override {
        filesDropped(juce::StringArray(toFile(text).getFullPathName()), x, y);
    }

private:
    static juce::File toFile(const juce::String& text) {
        const auto line = text.upToFirstOccurrenceOf("\n", false, false).trim();
        const auto path = line.startsWith("file://") ? juce::URL(line).getLocalFile().getFullPathName() : line;
        return juce::File::isAbsolutePath(path) && juce::File(path).existsAsFile() ? juce::File(path) : juce::File{};
    }

    void chooseFile(size_t track) {
        chooser = std::make_unique<juce::FileChooser>("Load sample for track " + juce::String(track + 1), juce::File{},
                                                      "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this, track](const juce::FileChooser& fc) {
                                 if (fc.getResult().existsAsFile()) load(track, fc.getResult());
                             });
    }

    void load(size_t track, const juce::File& file) {
        if (!proc.loadSample(track, file, true))
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Mensis Sampler",
                                                   "Couldn't load " + file.getFileName());
        timerCallback();
    }

    void timerCallback() override {
        for (auto& s : strips) s->refresh();
    }

    static constexpr int kHeader = 96;
    MensisLook look; // declared first so it outlives every child that uses it
    MensisProcessor& proc;
    std::array<std::unique_ptr<TrackStrip>, MensisProcessor::kNumTracks> strips;
    std::array<Knob, 5> globalKnobs;
    Knob volume;
    std::array<juce::TextButton, 7> pages; // LOOP / GRAINS / RESO / COLOR / EQ / SPACE / MOD
    SpacePanel spacePanel{proc};
    ModPanel modPanel{proc};
    SceneBar sceneBar{proc};
    int mapArmed = -1; // mod slot waiting for a knob to be touched

    void arm(int slot) {
        mapArmed = slot;
        modPanel.setArmed(slot);
        repaint(); // header hint
    }
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips{this, 500}; // shows the buttons' explanations after half a second
};

// The plugin window: drag the corner to resize; the UI scales as a whole, keeping its shape.
class MensisEditor final : public juce::AudioProcessorEditor {
public:
    explicit MensisEditor(MensisProcessor& p) : AudioProcessorEditor(p), proc(p), panel(p) {
        addAndMakeVisible(panel);
        constexpr double w = MensisPanel::kWidth, h = MensisPanel::kHeight;
        const double scale = p.getUiScale(); // read first: setResizeLimits resizes the window, and resized() stores a new scale
        setResizable(true, true);
        setResizeLimits(juce::roundToInt(w * 0.6), juce::roundToInt(h * 0.6), juce::roundToInt(w * 1.6), juce::roundToInt(h * 1.6));
        getConstrainer()->setFixedAspectRatio(w / h);
        setSize(juce::roundToInt(w * scale), juce::roundToInt(h * scale)); // the size you left it at
    }

    void resized() override {
        const float scale = static_cast<float>(getWidth()) / static_cast<float>(MensisPanel::kWidth);
        panel.setTransform(juce::AffineTransform::scale(scale));
        proc.setUiScale(scale);
    }

private:
    MensisProcessor& proc;
    MensisPanel panel;
};
