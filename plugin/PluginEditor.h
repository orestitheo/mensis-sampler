#pragma once
#include "PluginProcessor.h"

// Black and white look: thin arc knobs (like Live's), outlined buttons that fill white when on.
class MensisLook final : public juce::LookAndFeel_V4 {
public:
    static inline const juce::Colour bg{0xff000000}, fg{0xffffffff}, dim{0xff3a3a3a}, mid{0xff8a8a8a};

    MensisLook() {
        setColour(juce::ResizableWindow::backgroundColourId, bg);
        setColour(juce::Label::textColourId, fg);
        setColour(juce::Slider::textBoxTextColourId, fg);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, mid);
        setColour(juce::TextEditor::textColourId, fg);
        setColour(juce::TextEditor::focusedOutlineColourId, fg);
        setColour(juce::CaretComponent::caretColourId, fg);
        setColour(juce::TextButton::textColourOffId, fg);
        setColour(juce::TextButton::textColourOnId, bg);
        setColour(juce::AlertWindow::backgroundColourId, bg);
        setColour(juce::AlertWindow::textColourId, fg);
        setColour(juce::AlertWindow::outlineColourId, fg);
    }

    static juce::Font font(float size, bool bold = false) {
        return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle,
                          float endAngle, juce::Slider& s) override {
        const auto box = juce::Rectangle<int>(x, y, w, h).toFloat();
        const float r = std::min(box.getWidth(), box.getHeight()) * 0.5f - 3.0f;
        const auto c = box.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        // Bipolar params (Pan) draw their arc from the middle, like Live does.
        const float from = s.getMinimum() < 0.0 && s.getMaximum() > 0.0
            ? startAngle + static_cast<float>(s.valueToProportionOfLength(0.0)) * (endAngle - startAngle) : startAngle;
        const juce::PathStrokeType stroke(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::butt);

        juce::Path track, value;
        track.addCentredArc(c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
        g.setColour(dim);
        g.strokePath(track, stroke);
        value.addCentredArc(c.x, c.y, r, r, 0.0f, std::min(from, angle), std::max(from, angle), true);
        g.setColour(s.isEnabled() ? fg : mid);
        g.strokePath(value, stroke);

        const auto tip = c.getPointOnCircumference(r, angle), inner = c.getPointOnCircumference(r * 0.3f, angle);
        g.drawLine({inner, tip}, 2.0f);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
        const auto box = b.getLocalBounds().toFloat().reduced(0.5f);
        if (b.getToggleState() || down) {
            g.setColour(fg);
            g.fillRect(box);
        } else {
            g.setColour(over ? fg.withAlpha(0.12f) : bg);
            g.fillRect(box);
        }
        g.setColour(b.getToggleState() ? fg : mid);
        g.drawRect(box, 1.0f);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int) override { return font(11.0f, true); }
    juce::Font getLabelFont(juce::Label& l) override { return l.getFont(); }
    juce::Label* createSliderTextBox(juce::Slider& s) override {
        auto* l = LookAndFeel_V4::createSliderTextBox(s);
        l->setFont(font(11.0f));
        l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour(juce::Label::outlineWhenEditingColourId, fg);
        return l;
    }
};

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
        return {st, st + value("length") * (1.0f - st)};
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

    // Region as 0..1 of the sample -> Start and Length params (Length = share of what's after Start).
    void apply(float st, float en) {
        st = std::clamp(st, 0.0f, 0.99f);
        set("start", st);
        set("length", std::clamp((en - st) / (1.0f - st), 0.01f, 1.0f));
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

// Play button: white box with a black triangle when the track plays, dim outline when it's off.
class PlayButton final : public juce::Button {
public:
    PlayButton() : juce::Button("Play") { setClickingTogglesState(true); }

    void paintButton(juce::Graphics& g, bool over, bool) override {
        const auto box = getLocalBounds().toFloat().reduced(0.5f);
        const bool on = getToggleState();
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

// A Live-style knob: name above, value below. Drag up/down, double-click resets.
class Knob final : public juce::Component {
public:
    void attach(juce::RangedAudioParameter& param, const juce::String& name) {
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 14);
        label.setText(name, juce::dontSendNotification);
        label.setFont(MensisLook::font(10.0f, true));
        label.setJustificationType(juce::Justification::centred);
        attachment = std::make_unique<juce::SliderParameterAttachment>(param, slider);
        slider.setDoubleClickReturnValue(true, param.convertFrom0to1(param.getDefaultValue()));
        addAndMakeVisible(slider);
        addAndMakeVisible(label);
    }

    void resized() override {
        auto area = getLocalBounds();
        label.setBounds(area.removeFromTop(14));
        slider.setBounds(area);
    }

private:
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
};

// One track: number, Load button, file name; waveform on the left, knobs and switches on the right.
class TrackStrip final : public juce::Component {
public:
    static constexpr int kKnobAreaWidth = 9 * 60; // knob columns + toggle column, right of the waveform
    std::function<void()> onLoad;

    TrackStrip(MensisProcessor& p, size_t t) : proc(p), track(t), wave(p, t) {
        // Track number = on/off button (like Live's track activator): lit = playing.
        number.setText(juce::String(t + 1), juce::dontSendNotification);
        number.setFont(MensisLook::font(22.0f, true));
        play.setTooltip("Play / mute this track");
        playAttachment = std::make_unique<juce::ButtonParameterAttachment>(*p.params.getParameter(id("on")), play);
        addAndMakeVisible(number);
        addAndMakeVisible(play);
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
        const std::array<std::pair<const char*, const char*>, 3> toggleIds{
            {{"reverse", "REV"}, {"warp", "WARP"}, {"slice", "SLICE"}}};
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
    }

    // 0 = LOOP knobs, 1 = GRAINS knobs, 2 = RESONATOR + filter, 3 = COLOR. The waveform and track header stay.
    void setPage(int page) {
        for (auto& k : resKnobs) k.setVisible(page == 2);
        for (auto& k : colorKnobs) k.setVisible(page == 3);
        for (auto& k : knobs) k.setVisible(page == 0);
        for (auto& t : toggles) t.button.setVisible(page == 0);
        for (auto& k : grainKnobs) k.setVisible(page == 1);
        freeze.button.setVisible(page == 1);
    }

    void refresh() {
        const auto file = proc.getSampleFile(track);
        name.setText(file == juce::File{} ? "empty" : file.getFileNameWithoutExtension(), juce::dontSendNotification);
        knobs[3].setEnabled(toggles[1].button.getToggleState()); // Pitch only works with Warp on
        remove.setEnabled(file != juce::File{});
        wave.setAlpha(play.getToggleState() ? 1.0f : 0.3f); // muted tracks fade back
        number.setColour(juce::Label::textColourId, play.getToggleState() ? MensisLook::fg : MensisLook::mid);
        wave.repaint();
    }

    void paint(juce::Graphics& g) override {
        g.setColour(MensisLook::dim);
        g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
    }

    void resized() override {
        auto area = getLocalBounds().reduced(0, 10);
        auto head = area.removeFromTop(24);
        number.setBounds(head.removeFromLeft(26));
        play.setBounds(head.removeFromLeft(28).reduced(2, 1));
        head.removeFromLeft(8);
        load.setBounds(head.removeFromLeft(56).reduced(0, 2));
        head.removeFromLeft(4);
        remove.setBounds(head.removeFromLeft(24).reduced(0, 2));
        name.setBounds(head.withTrimmedLeft(8));
        area.removeFromTop(6);

        auto right = area.removeFromRight(kKnobAreaWidth);
        wave.setBounds(area.withTrimmedRight(16));
        right = right.withSizeKeepingCentre(right.getWidth(), std::min(right.getHeight(), 96)); // knobs stay knob-sized
        for (size_t i = 0; i < knobs.size(); ++i) {
            const auto col = right.removeFromLeft(60);
            knobs[i].setBounds(col);
            grainKnobs[i].setBounds(col); // same columns, other page
            resKnobs[i].setBounds(col);
            colorKnobs[i].setBounds(col);
        }
        resKnobs[7].setBounds(right.withWidth(60)); // 8th knob where the toggles are on the other pages
        colorKnobs[7].setBounds(right.withWidth(60));
        right = right.withSizeKeepingCentre(right.getWidth() - 8, 74);
        freeze.button.setBounds(right.withHeight(22).reduced(4, 0));
        for (auto& t : toggles) {
            t.button.setBounds(right.removeFromTop(22).reduced(4, 0));
            right.removeFromTop(4);
        }
    }

private:
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
    std::array<Toggle, 3> toggles;
    std::array<Knob, 7> grainKnobs;
    std::array<Knob, 8> resKnobs, colorKnobs;
    Toggle freeze;
    juce::Label number;
    PlayButton play;
    std::unique_ptr<juce::ButtonParameterAttachment> playAttachment;
};

// SPACE page: one SEND per track (lined up with its strip) and the shared delay + reverb controls.
class SpacePanel final : public juce::Component {
public:
    explicit SpacePanel(MensisProcessor& p) {
        for (size_t t = 0; t < sends.size(); ++t) {
            sends[t].attach(*p.params.getParameter("t" + juce::String(t + 1) + ".send"), "SEND");
            addAndMakeVisible(sends[t]);
        }
        const std::array<std::pair<const char*, const char*>, 6> ids{{{"space.time", "TIME"}, {"space.feedback", "FEEDBACK"},
            {"space.blend", "BLEND"}, {"space.size", "SIZE"}, {"space.tone", "TONE"}, {"space.return", "RETURN"}}};
        for (size_t i = 0; i < knobs.size(); ++i) {
            knobs[i].attach(*p.params.getParameter(ids[i].first), ids[i].second);
            addAndMakeVisible(knobs[i]);
        }
        freeze.setButtonText("FREEZE");
        freeze.setClickingTogglesState(true);
        freezeAttachment = std::make_unique<juce::ButtonParameterAttachment>(*p.params.getParameter("space.freeze"), freeze);
        addAndMakeVisible(freeze);
    }

    // Where each track strip sits, in this panel's coordinates, so its SEND lines up with it.
    void setRows(const std::array<juce::Rectangle<int>, MensisProcessor::kNumTracks>& r) { rows = r; resized(); }

    void paint(juce::Graphics& g) override {
        g.setColour(MensisLook::dim);
        g.fillRect(static_cast<float>(kSendWidth) + 10.0f, 16.0f, 1.0f, static_cast<float>(getHeight()) - 32.0f);
        auto title = controlsArea().translated(0, -44).withHeight(36);
        g.setColour(MensisLook::fg);
        g.setFont(MensisLook::font(13.0f, true));
        g.drawText("S P A C E", title, juce::Justification::bottomLeft);
        g.setColour(MensisLook::mid);
        g.setFont(MensisLook::font(10.0f));
        g.drawText("SHARED DELAY + REVERB", title, juce::Justification::bottomRight);
    }

    void resized() override {
        for (size_t t = 0; t < sends.size(); ++t)
            sends[t].setBounds(rows[t].withX(0).withWidth(kSendWidth).withSizeKeepingCentre(kSendWidth, std::min(rows[t].getHeight(), 96)));
        auto area = controlsArea();
        auto top = area.removeFromTop(kRowH), bottom = area.removeFromTop(kRowH);
        const int w = top.getWidth() / 4;
        for (size_t i = 0; i < 3; ++i) knobs[i].setBounds(top.removeFromLeft(w).withSizeKeepingCentre(64, 96));
        for (size_t i = 3; i < 6; ++i) knobs[i].setBounds(bottom.removeFromLeft(w).withSizeKeepingCentre(64, 96));
        freeze.setBounds(bottom.removeFromLeft(w).withSizeKeepingCentre(80, 24));
    }

private:
    static constexpr int kSendWidth = 64, kRowH = 110;
    juce::Rectangle<int> controlsArea() const { // two rows of knobs, centred right of the sends
        const auto r = getLocalBounds().withTrimmedLeft(kSendWidth + 24);
        return r.withSizeKeepingCentre(r.getWidth(), 2 * kRowH).translated(0, 20);
    }
    std::array<Knob, MensisProcessor::kNumTracks> sends;
    std::array<Knob, 6> knobs;
    juce::TextButton freeze;
    std::unique_ptr<juce::ButtonParameterAttachment> freezeAttachment;
    std::array<juce::Rectangle<int>, MensisProcessor::kNumTracks> rows{};
};

class MensisEditor final : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget,
                           public juce::TextDragAndDropTarget, private juce::Timer {
public:
    explicit MensisEditor(MensisProcessor& p) : AudioProcessorEditor(p), proc(p) {
        setLookAndFeel(&look);
        for (size_t t = 0; t < MensisProcessor::kNumTracks; ++t) {
            strips[t] = std::make_unique<TrackStrip>(p, t);
            strips[t]->onLoad = [this, t] { chooseFile(t); };
            addAndMakeVisible(*strips[t]);
        }
        const std::array<std::pair<const char*, const char*>, 4> globalIds{
            {{"chaos", "CHAOS"}, {"chaos.grid", "GRID"}, {"chaos.feel", "FEEL"}, {"seed", "SEED"}}};
        for (size_t i = 0; i < globalKnobs.size(); ++i) {
            globalKnobs[i].attach(*p.params.getParameter(globalIds[i].first), globalIds[i].second);
            addAndMakeVisible(globalKnobs[i]);
        }
        volume.attach(*p.params.getParameter("master"), "VOLUME");
        addAndMakeVisible(volume);
        reroll.setButtonText("RE-ROLL");
        reroll.onClick = [this] { // new seed; the audio switches to it at the next bar
            auto* seed = proc.params.getParameter("seed");
            const int now = static_cast<int>(seed->convertFrom0to1(seed->getValue()));
            const int next = (now + 1 + juce::Random::getSystemRandom().nextInt(9999)) % 10000; // never the same seed
            seed->beginChangeGesture();
            seed->setValueNotifyingHost(seed->convertTo0to1(static_cast<float>(next)));
            seed->endChangeGesture();
        };
        addAndMakeVisible(reroll);

        const std::array<const char*, 5> pageNames{"LOOP", "GRAINS", "RESONATOR", "COLOR", "SPACE"};
        for (int i = 0; i < 5; ++i) {
            auto& b = pages[static_cast<size_t>(i)];
            b.setButtonText(pageNames[static_cast<size_t>(i)]);
            b.setRadioGroupId(1);
            b.setClickingTogglesState(true);
            b.onClick = [this, i] {
                for (auto& s : strips) s->setPage(i);
                spacePanel.setVisible(i == 4);
            };
            addAndMakeVisible(b);
        }
        pages[0].setToggleState(true, juce::dontSendNotification);
        addChildComponent(spacePanel); // shown on the SPACE page
        sendLookAndFeelChange(); // the strips were built before they had a parent; restyle their knob value boxes
        setSize(1040, 840); // tall strips: room for a big waveform
        timerCallback();
        startTimerHz(30); // moves the playheads; also picks up samples restored from a saved Live set
    }

    ~MensisEditor() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override {
        g.fillAll(MensisLook::bg);
        auto head = getLocalBounds().reduced(20, 0).removeFromTop(kHeader);
        g.setColour(MensisLook::fg);
        g.setFont(MensisLook::font(16.0f, true));
        g.drawText("M E N S I S", head.removeFromTop(kHeader / 2 + 8), juce::Justification::bottomLeft);
        g.setColour(MensisLook::mid);
        g.setFont(MensisLook::font(11.0f));
        g.drawText("SAMPLER", head, juce::Justification::topLeft);
    }

    void resized() override {
        auto head = getLocalBounds().reduced(20, 0).removeFromTop(kHeader).reduced(0, 12);
        volume.setBounds(head.removeFromRight(70)); // master, far right
        head.removeFromRight(20);
        reroll.setBounds(head.removeFromRight(80).withSizeKeepingCentre(80, 24));
        head.removeFromRight(8);
        for (size_t i = globalKnobs.size(); i-- > 0;) globalKnobs[i].setBounds(head.removeFromRight(i == 0 ? 76 : 60));
        auto tabs = head.withTrimmedLeft(160).removeFromLeft(5 * 80).withSizeKeepingCentre(5 * 80, 24);
        for (auto& b : pages) b.setBounds(tabs.removeFromLeft(80).withTrimmedRight(4));

        auto area = getLocalBounds().reduced(20, 0).withTrimmedTop(kHeader).withTrimmedBottom(12);
        const int h = area.getHeight() / static_cast<int>(MensisProcessor::kNumTracks);
        const auto stripsArea = area;
        for (auto& s : strips) s->setBounds(area.removeFromTop(h));
        // SPACE page panel sits over the strips' knob area (the strips hide their knobs on that page).
        spacePanel.setBounds(stripsArea.withTrimmedLeft(stripsArea.getWidth() - TrackStrip::kKnobAreaWidth));
        std::array<juce::Rectangle<int>, MensisProcessor::kNumTracks> rows;
        for (size_t t = 0; t < rows.size(); ++t)
            rows[t] = strips[t]->getBounds().withTrimmedTop(40).translated(-spacePanel.getX(), -spacePanel.getY());
        spacePanel.setRows(rows);
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
    std::array<Knob, 4> globalKnobs;
    Knob volume;
    juce::TextButton reroll;
    std::array<juce::TextButton, 5> pages; // LOOP / GRAINS / RESONATOR / COLOR / SPACE
    SpacePanel spacePanel{proc};
    std::unique_ptr<juce::FileChooser> chooser;
};
