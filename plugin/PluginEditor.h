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
        if (sample != peaksFor || static_cast<int>(peaks.size()) != getWidth()) buildPeaks(*sample);

        const auto [st, en] = region();
        const float w = b.getWidth(), mid = b.getCentreY(), half = b.getHeight() * 0.5f - 2.0f;
        g.setColour(MensisLook::fg.withAlpha(grab == Grab::New ? 0.16f : 0.08f));
        g.fillRect(b.withX(st * w).withRight(en * w));
        for (size_t x = 0; x < peaks.size(); ++x) {
            const float fx = static_cast<float>(x) / w;
            g.setColour(fx >= st && fx <= en ? MensisLook::fg : MensisLook::dim); // only the loop region is lit
            g.drawVerticalLine(static_cast<int>(x), mid - peaks[x] * half, mid + peaks[x] * half);
        }
        if (value("slice") > 0.5f) { // hit markers inside the loop region: thin line + a tick at the bottom
            const float n = static_cast<float>(std::max(1, sample->audio.getNumSamples() - 1));
            for (int onset : sample->slices) {
                const float fx = static_cast<float>(onset) / n;
                if (fx <= st || fx >= en) continue; // only the region gets sliced
                const float x = fx * w;
                g.setColour(MensisLook::fg.withAlpha(0.3f));
                g.drawVerticalLine(static_cast<int>(x), 0.0f, b.getHeight());
                g.setColour(MensisLook::fg);
                g.fillRect(x - 0.5f, b.getHeight() - 6.0f, 2.0f, 6.0f);
            }
        }
        for (const auto [edge, x] : {std::pair{Grab::Start, st * w}, std::pair{Grab::End, en * w - 2.0f}}) {
            const bool active = grab == edge || (grab == Grab::None && hover == edge);
            g.setColour(active ? MensisLook::fg : MensisLook::fg.withAlpha(0.7f));
            g.fillRect(x, 0.0f, active ? 3.0f : 2.0f, b.getHeight());
            // small tab at the top, pointing into the region, so the edges look grabbable
            g.fillRect(edge == Grab::Start ? x : x - 6.0f, 0.0f, 8.0f, 10.0f);
        }
        drawCloud(g, b, *sample);
        if (const float pos = proc.getPlayPosition(track); pos >= 0.0f) {
            g.setColour(MensisLook::fg);
            g.fillRect(pos * w - 0.5f, 0.0f, 1.0f, b.getHeight());
            g.fillEllipse(pos * w - 3.0f, -3.0f, 6.0f, 6.0f);
        }
    }

    // Drag anywhere: the part you sweep over becomes the loop. Drag an edge: resize that side.
    // Double-click: the whole sample.
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
    // The grain cloud: one dot per grain at the spot it reads, higher = panned left, lower = right.
    // Dots swell and fade with the grain's window and leave a short trail; backwards grains are rings.
    // Under it: the spray range as a thin bar, and when frozen a dotted line with a diamond on top.
    void drawCloud(juce::Graphics& g, juce::Rectangle<float> b, const mensis::Sample& sample) const {
        const float anchor = proc.getGrainAnchor(track);
        if (anchor < 0.0f) return; // cloud off
        const float w = b.getWidth(), h = b.getHeight(), mid = b.getCentreY();

        const float ax = anchor * w;
        const float spray = value("grain.spray") * 0.5f * static_cast<float>(sample.sampleRate)
                            / static_cast<float>(std::max(1, sample.audio.getNumSamples())) * w;
        g.setColour(MensisLook::fg.withAlpha(0.25f));
        g.fillRect(juce::Rectangle<float>(ax - spray, h - 3.0f, 2.0f * spray, 1.0f).getIntersection(b));
        g.setColour(MensisLook::fg.withAlpha(0.6f));
        g.fillRect(ax - 0.5f, h - 5.0f, 1.0f, 5.0f); // where the cloud is centred

        if (value("grain.freeze") > 0.5f) {
            g.setColour(MensisLook::fg);
            for (float y = 10.0f; y < h - 6.0f; y += 5.0f) g.fillRect(ax - 0.5f, y, 1.0f, 2.0f);
            juce::Path diamond;
            diamond.addQuadrilateral(ax, 1.0f, ax + 4.0f, 5.0f, ax, 9.0f, ax - 4.0f, 5.0f);
            g.fillPath(diamond);
        }

        for (size_t i = 0; i < MensisProcessor::kMaxGrains; ++i) {
            const auto d = proc.getGrainDot(track, i);
            if (d.env <= 0.01f) continue;
            const float x = d.x * w, y = mid - d.pan * (h * 0.5f - 8.0f);
            const float r = 1.5f + 2.5f * d.env;
            const float trail = 10.0f * d.env * (d.backwards ? 1.0f : -1.0f); // behind the dot
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
        const float w = static_cast<float>(getWidth());
        const float ds = std::abs(px - st * w), de = std::abs(px - en * w);
        if (std::min(ds, de) <= 6.0f) return ds < de ? Grab::Start : Grab::End; // 6 px grab zone per edge
        return Grab::New;
    }

    // Region as 0..1 of the sample -> Start and Length params (Length = share of what's after Start).
    void apply(float st, float en) {
        st = std::clamp(st, 0.0f, 0.99f);
        set("start", st);
        set("length", std::clamp((en - st) / (1.0f - st), 0.01f, 1.0f));
    }

    float xNorm(const juce::MouseEvent& e) const {
        return std::clamp(e.position.x / static_cast<float>(std::max(1, getWidth())), 0.0f, 1.0f);
    }

    void buildPeaks(const mensis::Sample& sample) { // biggest |level| per pixel column, rebuilt on load/resize
        peaksFor = &sample;
        peaks.assign(static_cast<size_t>(std::max(0, getWidth())), 0.0f);
        const int n = sample.audio.getNumSamples();
        for (size_t x = 0; x < peaks.size(); ++x) {
            const int a = static_cast<int>(x * n / peaks.size()), z = static_cast<int>((x + 1) * n / peaks.size());
            for (int ch = 0; ch < sample.audio.getNumChannels(); ++ch)
                peaks[x] = std::max(peaks[x], sample.audio.getMagnitude(ch, a, std::max(1, z - a)));
        }
    }

    MensisProcessor& proc;
    size_t track;
    const mensis::Sample* peaksFor = nullptr;
    std::vector<float> peaks;
    Grab grab = Grab::None, hover = Grab::None;
    float downStart = 0.0f, downEnd = 1.0f, downX = 0.0f;
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
    std::function<void()> onLoad;

    TrackStrip(MensisProcessor& p, size_t t) : proc(p), track(t), wave(p, t) {
        number.setText(juce::String(t + 1), juce::dontSendNotification);
        number.setFont(MensisLook::font(22.0f, true));
        name.setFont(MensisLook::font(12.0f));
        name.setColour(juce::Label::textColourId, MensisLook::mid);
        load.setButtonText("LOAD");
        load.onClick = [this] { if (onLoad) onLoad(); };
        for (auto* c : std::initializer_list<juce::Component*>{&number, &name, &load, &wave}) addAndMakeVisible(c);

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
    }

    // 0 = LOOP knobs, 1 = GRAINS knobs. The waveform and track header stay.
    void setPage(int page) {
        for (auto& k : knobs) k.setVisible(page == 0);
        for (auto& t : toggles) t.button.setVisible(page == 0);
        for (auto& k : grainKnobs) k.setVisible(page == 1);
        freeze.button.setVisible(page == 1);
    }

    void refresh() {
        const auto file = proc.getSampleFile(track);
        name.setText(file == juce::File{} ? "empty" : file.getFileNameWithoutExtension(), juce::dontSendNotification);
        knobs[3].setEnabled(toggles[1].button.getToggleState()); // Pitch only works with Warp on
        wave.repaint();
    }

    void paint(juce::Graphics& g) override {
        g.setColour(MensisLook::dim);
        g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
    }

    void resized() override {
        auto area = getLocalBounds().reduced(0, 10);
        auto head = area.removeFromTop(24);
        number.setBounds(head.removeFromLeft(28));
        load.setBounds(head.removeFromLeft(56).reduced(0, 2));
        name.setBounds(head.withTrimmedLeft(8));
        area.removeFromTop(6);

        auto right = area.removeFromRight(9 * 60);
        wave.setBounds(area.withTrimmedRight(16));
        for (size_t i = 0; i < knobs.size(); ++i) {
            const auto col = right.removeFromLeft(60);
            knobs[i].setBounds(col);
            grainKnobs[i].setBounds(col); // same columns, other page
        }
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
    juce::Label number, name;
    juce::TextButton load;
    WaveformView wave;
    std::array<Knob, 7> knobs;
    std::array<Toggle, 3> toggles;
    std::array<Knob, 7> grainKnobs;
    Toggle freeze;
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

        const std::array<const char*, 2> pageNames{"LOOP", "GRAINS"};
        for (int i = 0; i < 2; ++i) {
            auto& b = pages[static_cast<size_t>(i)];
            b.setButtonText(pageNames[static_cast<size_t>(i)]);
            b.setRadioGroupId(1);
            b.setClickingTogglesState(true);
            b.onClick = [this, i] { for (auto& s : strips) s->setPage(i); };
            addAndMakeVisible(b);
        }
        pages[0].setToggleState(true, juce::dontSendNotification);
        sendLookAndFeelChange(); // the strips were built before they had a parent; restyle their knob value boxes
        setSize(1040, 640);
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
        reroll.setBounds(head.removeFromRight(80).withSizeKeepingCentre(80, 24));
        head.removeFromRight(8);
        for (size_t i = globalKnobs.size(); i-- > 0;) globalKnobs[i].setBounds(head.removeFromRight(i == 0 ? 76 : 60));
        auto tabs = head.withTrimmedLeft(180).removeFromLeft(2 * 84).withSizeKeepingCentre(2 * 84, 24);
        for (auto& b : pages) b.setBounds(tabs.removeFromLeft(84).withTrimmedRight(4));

        auto area = getLocalBounds().reduced(20, 0).withTrimmedTop(kHeader).withTrimmedBottom(12);
        const int h = area.getHeight() / static_cast<int>(MensisProcessor::kNumTracks);
        for (auto& s : strips) s->setBounds(area.removeFromTop(h));
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
    juce::TextButton reroll;
    std::array<juce::TextButton, 2> pages; // LOOP / GRAINS
    std::unique_ptr<juce::FileChooser> chooser;
};
