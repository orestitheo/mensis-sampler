#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>

// Shared by Mensis Sampler and Mensis FX, so both look the same.
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
        setColour(juce::TooltipWindow::backgroundColourId, bg);
        setColour(juce::TooltipWindow::textColourId, fg);
        setColour(juce::TooltipWindow::outlineColourId, mid);
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
        if (!b.isEnabled()) { // off: a dim outline only, even if it's switched on underneath
            g.setColour(dim);
            g.drawRect(box, 1.0f);
            return;
        }
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
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down) override {
        g.setFont(getTextButtonFont(b, b.getHeight()));
        g.setColour(!b.isEnabled() ? mid.withAlpha(0.6f) : b.getToggleState() || down ? bg : fg); // black on the white fill
        g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
    }
    juce::Font getLabelFont(juce::Label& l) override { return l.getFont(); }
    juce::Label* createSliderTextBox(juce::Slider& s) override {
        auto* l = LookAndFeel_V4::createSliderTextBox(s);
        l->setFont(font(11.0f));
        l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour(juce::Label::outlineWhenEditingColourId, fg);
        return l;
    }
};

// A Live-style knob: name above, value below. Drag up/down, double-click resets.
// What a knob needs from the editor it sits in (found by walking up its parents, so each plugin
// instance's knobs talk to their own editor).
struct KnobHost {
    virtual ~KnobHost() = default;
    virtual std::optional<float> modulatedValue(const juce::RangedAudioParameter&) = 0; // 0..1 if modulated
    virtual void knobTouched(juce::RangedAudioParameter&) = 0;                          // MAP picks targets here
};

class Knob final : public juce::Component, private juce::Timer {
public:
    void attach(juce::RangedAudioParameter& p, const juce::String& name) {
        param = &p;
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 14);
        label.setText(name, juce::dontSendNotification);
        label.setFont(MensisLook::font(10.0f, true));
        label.setJustificationType(juce::Justification::centred);
        attachment = std::make_unique<juce::SliderParameterAttachment>(p, slider);
        slider.setDoubleClickReturnValue(true, p.convertFrom0to1(p.getDefaultValue()));
        slider.onDragStart = [this] {
            if (auto* host = findParentComponentOfClass<KnobHost>()) host->knobTouched(*param);
        };
        addAndMakeVisible(slider);
        addAndMakeVisible(label);
        startTimerHz(30);
    }

    void resized() override {
        auto area = getLocalBounds();
        label.setBounds(area.removeFromTop(14));
        slider.setBounds(area);
    }

    // Modulation: a faint arc from the knob's setting to where it is now, and a dot at the moving value.
    void paintOverChildren(juce::Graphics& g) override {
        if (!modded) return;
        const auto box = slider.getLookAndFeel().getSliderLayout(slider).sliderBounds.toFloat() + slider.getPosition().toFloat();
        const float r = std::min(box.getWidth(), box.getHeight()) * 0.5f - 3.0f;
        const auto c = box.getCentre();
        const auto rp = slider.getRotaryParameters();
        auto angleOf = [&](float norm) { return rp.startAngleRadians + norm * (rp.endAngleRadians - rp.startAngleRadians); };
        const float from = angleOf(param->getValue()), to = angleOf(modValue);
        juce::Path arc;
        arc.addCentredArc(c.x, c.y, r + 5.0f, r + 5.0f, 0.0f, std::min(from, to), std::max(from, to), true);
        g.setColour(MensisLook::fg.withAlpha(0.45f));
        g.strokePath(arc, juce::PathStrokeType(1.5f));
        const auto dot = c.getPointOnCircumference(r + 5.0f, to);
        g.setColour(MensisLook::fg);
        g.fillEllipse(dot.x - 3.0f, dot.y - 3.0f, 6.0f, 6.0f);
    }

private:
    void timerCallback() override {
        auto* host = isShowing() ? findParentComponentOfClass<KnobHost>() : nullptr;
        const auto v = host != nullptr && param != nullptr ? host->modulatedValue(*param) : std::nullopt;
        if (v.has_value() != modded || (v && std::abs(*v - modValue) > 0.002f)) {
            modded = v.has_value();
            modValue = v.value_or(0.0f);
            repaint();
        }
    }

    juce::RangedAudioParameter* param = nullptr;
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    bool modded = false;
    float modValue = 0.0f;
};
