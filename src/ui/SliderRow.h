#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace koenote::ui
{
// ラベルとスライダーの組。サイドパネルで共通に使う。
struct SliderRow
{
    enum class Format
    {
        Percent, // 値 1.0 = 100%
        Db,
        Cents,
        Plain
    };

    juce::Label label;
    juce::Slider slider;

    void init(juce::Component& parent, double minimum, double maximum, double step, double resetValue, Format format)
    {
        label.setColour(juce::Label::textColourId, juce::Colour(0xffd5dae3));
        label.setFont(juce::FontOptions(12.0f));
        parent.addAndMakeVisible(label);

        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 20);
        slider.setRange(minimum, maximum, step);
        slider.setDoubleClickReturnValue(true, resetValue);

        switch (format)
        {
            case Format::Percent:
                slider.textFromValueFunction = [] (double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; };
                slider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue() / 100.0; };
                break;
            case Format::Db:
                slider.textFromValueFunction = [] (double v) { return juce::String(v, 1) + " dB"; };
                slider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
                break;
            case Format::Cents:
                slider.textFromValueFunction = [] (double v) { return juce::String(juce::roundToInt(v)) + " ct"; };
                slider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
                break;
            case Format::Plain: break;
        }

        slider.updateText();
        parent.addAndMakeVisible(slider);
    }

    // 縦に並べるときの高さ（ラベル 18 + スライダー 24 + 余白 8）
    static constexpr int kHeight = 50;

    void place(juce::Rectangle<int>& area)
    {
        label.setBounds(area.removeFromTop(18));
        slider.setBounds(area.removeFromTop(24));
        area.removeFromTop(8);
    }

    void setEnabledState(bool enabled)
    {
        slider.setEnabled(enabled);
        label.setEnabled(enabled);
    }
};

inline void styleSectionLabel(juce::Label& l)
{
    l.setColour(juce::Label::textColourId, juce::Colour(0xfff2b441));
    l.setFont(juce::FontOptions(13.0f, juce::Font::bold));
}

inline void styleHint(juce::Label& l)
{
    l.setColour(juce::Label::textColourId, juce::Colour(0xff8a93a3));
    l.setFont(juce::FontOptions(11.5f));
    l.setMinimumHorizontalScale(1.0f);
}
} // namespace koenote::ui
