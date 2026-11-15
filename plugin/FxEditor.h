#pragma once
#include "Look.h"

// Mensis FX window: the sampler's look, one row per device (title on the left, grouped knobs on the right).
class FxPanel final : public juce::Component {
public:
    static constexpr int kWidth = 760, kHeight = 640;

    explicit FxPanel(juce::AudioProcessorValueTreeState& s) : state(s) {
        setLookAndFeel(&look);
        rows = {{
            {"G R A I N S", "CLOUD FROM THE LAST SECONDS", {
                {"GRAIN", {{"grain.mix", "MIX"}, {"grain.size", "SIZE"}, {"grain.density", "DENSITY"}}, {{"grain.freeze", "FREEZE"}}},
                {"SCATTER", {{"grain.spray", "SPRAY"}, {"grain.detune", "DETUNE"}, {"grain.reverse", "REVERSE"},
                             {"grain.width", "WIDTH"}, {"chaos", "CHAOS"}}, {}}}},
            {"R E S O", "TUNED RESONATOR + FILTER", {
                {"RESONATOR", {{"res.mix", "MIX"}, {"res.root", "ROOT"}, {"res.chord", "CHORD"}, {"res.decay", "DECAY"},
                               {"res.color", "COLOR"}}, {}},
                {"FILTER", {{"flt.cutoff", "CUTOFF"}, {"flt.reso", "RESO"}, {"flt.morph", "MORPH"}}, {}}}},
            {"C O L O R", "DRIVE, CRUSH, SQUASH", {
                {"SATURATE", {{"col.drive", "DRIVE"}, {"col.char", "CHARACTER"}}, {}},
                {"CRUSH", {{"col.bits", "BITS"}, {"col.rate", "RATE"}}, {}},
                {"SHAPE", {{"col.squash", "SQUASH"}, {"col.tilt", "TILT"}}, {}},
                {"OUT", {{"col.out", "OUTPUT"}, {"col.mix", "MIX"}}, {}}}},
            {"S P A C E", "DELAY + REVERB", {
                {"SEND", {{"space.send", "SEND"}}, {}},
                {"DELAY", {{"space.time", "TIME"}, {"space.feedback", "FEEDBACK"}}, {}},
                {"REVERB", {{"space.blend", "BLEND"}, {"space.size", "SIZE"}, {"space.tone", "TONE"}}, {}},
                {"OUT", {{"space.return", "RETURN"}}, {{"space.freeze", "FREEZE"}}}}},
        }};
        for (auto& row : rows)
            for (auto& group : row.groups) {
                for (auto& k : group.knobs) {
                    k.knob = ownedKnobs.add(new Knob());
                    k.knob->attach(*state.getParameter(k.id), k.name);
                    addAndMakeVisible(*k.knob);
                }
                for (auto& b : group.buttons) {
                    b.button = ownedButtons.add(new juce::TextButton(b.name));
                    b.button->setClickingTogglesState(true);
                    attachments.add(new juce::ButtonParameterAttachment(*state.getParameter(b.id), *b.button));
                    addAndMakeVisible(*b.button);
                }
            }
        rows[0].groups[0].buttons[0].button->setTooltip("Stop recording: the cloud keeps playing what it holds");
        rows[3].groups[3].buttons[0].button->setTooltip("Hold whatever is ringing in the Space, forever");
        seed.attach(*state.getParameter("seed"), "SEED");
        addAndMakeVisible(seed);
        sendLookAndFeelChange(); // restyle the knob value boxes now they have the look
        setSize(kWidth, kHeight);
    }

    ~FxPanel() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& g) override {
        g.fillAll(MensisLook::bg);
        auto head = getLocalBounds().reduced(20, 0).removeFromTop(kHeader);
        g.setColour(MensisLook::fg);
        g.setFont(MensisLook::font(16.0f, true));
        g.drawText("M E N S I S", head.removeFromTop(kHeader / 2 + 8), juce::Justification::bottomLeft);
        g.setColour(MensisLook::mid);
        g.setFont(MensisLook::font(11.0f));
        g.drawText("FX", head, juce::Justification::topLeft);

        for (const auto& row : rows) {
            g.setColour(MensisLook::dim);
            g.drawHorizontalLine(row.bounds.getY(), static_cast<float>(row.bounds.getX()), static_cast<float>(row.bounds.getRight()));
            auto title = row.bounds.withWidth(kTitleW).reduced(0, 14);
            g.setColour(MensisLook::fg);
            g.setFont(MensisLook::font(13.0f, true));
            g.drawText(row.title, title.removeFromTop(20), juce::Justification::topLeft);
            g.setColour(MensisLook::mid);
            g.setFont(MensisLook::font(9.0f));
            g.drawFittedText(row.subtitle, title.removeFromTop(28), juce::Justification::topLeft, 2);
            for (const auto& group : row.groups) { // tiny group name over a thin line, like the sampler
                if (group.header.isEmpty()) continue;
                g.setColour(MensisLook::mid);
                g.setFont(MensisLook::font(9.0f, true));
                g.drawText(group.name, group.header.withTrimmedBottom(4), juce::Justification::bottomLeft);
                g.setColour(MensisLook::dim);
                g.fillRect(group.header.getX(), group.header.getBottom() - 2, group.header.getWidth(), 1);
            }
        }
    }

    void resized() override {
        auto head = getLocalBounds().reduced(20, 0).removeFromTop(kHeader).reduced(0, 12);
        seed.setBounds(head.removeFromRight(60));

        auto area = getLocalBounds().reduced(20, 0).withTrimmedTop(kHeader).withTrimmedBottom(12);
        const int h = area.getHeight() / static_cast<int>(rows.size());
        for (auto& row : rows) {
            row.bounds = area.removeFromTop(h);
            auto right = row.bounds.withTrimmedLeft(kTitleW);
            const auto block = right.withSizeKeepingCentre(right.getWidth(), std::min(right.getHeight(), kHeaderH + 96));
            int x = block.getX();
            for (auto& group : row.groups) {
                const int w = static_cast<int>(group.knobs.size()) * kKnobW + (group.buttons.empty() ? 0 : kButtonW);
                group.header = {x, block.getY(), w, kHeaderH};
                auto r = juce::Rectangle<int>(x, block.getY() + kHeaderH, w, block.getHeight() - kHeaderH);
                for (auto& k : group.knobs) k.knob->setBounds(r.removeFromLeft(kKnobW));
                if (!group.buttons.empty()) {
                    auto stack = r.withSizeKeepingCentre(r.getWidth(), 20).translated(0, 4).reduced(4, 0);
                    for (auto& b : group.buttons) b.button->setBounds(stack);
                }
                x += w + kGap;
            }
        }
    }

private:
    static constexpr int kHeader = 96, kTitleW = 130, kKnobW = 54, kButtonW = 64, kGap = 10, kHeaderH = 16;

    struct KnobSlot { const char* id; const char* name; Knob* knob = nullptr; };
    struct ButtonSlot { const char* id; const char* name; juce::TextButton* button = nullptr; };
    struct Group { const char* name; std::vector<KnobSlot> knobs; std::vector<ButtonSlot> buttons; juce::Rectangle<int> header{}; };
    struct Row { const char* title; const char* subtitle; std::vector<Group> groups; juce::Rectangle<int> bounds{}; };

    MensisLook look; // declared first so it outlives every child that uses it
    juce::AudioProcessorValueTreeState& state;
    std::array<Row, 4> rows;
    juce::OwnedArray<Knob> ownedKnobs;
    juce::OwnedArray<juce::TextButton> ownedButtons;
    juce::OwnedArray<juce::ButtonParameterAttachment> attachments;
    Knob seed;
    juce::TooltipWindow tooltips{this, 500};
};

// The plugin window: drag the corner to resize; the UI scales as a whole, keeping its shape.
class FxEditor final : public juce::AudioProcessorEditor {
public:
    FxEditor(juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s) : AudioProcessorEditor(p), panel(s) {
        addAndMakeVisible(panel);
        constexpr double w = FxPanel::kWidth, h = FxPanel::kHeight;
        setResizable(true, true);
        setResizeLimits(juce::roundToInt(w * 0.6), juce::roundToInt(h * 0.6), juce::roundToInt(w * 1.6), juce::roundToInt(h * 1.6));
        getConstrainer()->setFixedAspectRatio(w / h);
        setSize(FxPanel::kWidth, FxPanel::kHeight);
    }

    void resized() override {
        panel.setTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / static_cast<float>(FxPanel::kWidth)));
    }

private:
    FxPanel panel;
};
