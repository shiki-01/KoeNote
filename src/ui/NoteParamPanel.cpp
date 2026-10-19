#include "ui/NoteParamPanel.h"

#include <cmath>

namespace koenote::ui
{
NoteParamPanel::NoteParamPanel(PianoRollView& view) : roll(view)
{
    language = detectDefaultLanguage();

    setupRow(center, 1.0, [] (model::NoteEdit& e, float v) { e.centerCorrection = v; });
    setupRow(vibrato, 2.0, [] (model::NoteEdit& e, float v) { e.vibratoScale = v; });
    setupRow(drift, 2.0, [] (model::NoteEdit& e, float v) { e.driftScale = v; });
    setupRow(transition, 1.0, [] (model::NoteEdit& e, float v) { e.transitionSmoothness = v; });

    // 既定値へ戻しやすいよう、ダブルクリックで戻す値を設定する
    center.slider.setDoubleClickReturnValue(true, 0.0);
    vibrato.slider.setDoubleClickReturnValue(true, 1.0);
    drift.slider.setDoubleClickReturnValue(true, 1.0);
    transition.slider.setDoubleClickReturnValue(true, 0.0);

    pitchInfo.setColour(juce::Label::textColourId, juce::Colour(0xffd5dae3));
    pitchInfo.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(pitchInfo);

    emptyHint.setColour(juce::Label::textColourId, juce::Colour(0xff8a93a3));
    emptyHint.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(emptyHint);

    updateTexts();
    refresh();
}

void NoteParamPanel::setupRow(Row& row, double maximum, std::function<void(model::NoteEdit&, float)> setter)
{
    row.label.setColour(juce::Label::textColourId, juce::Colour(0xffd5dae3));
    row.label.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(row.label);

    row.slider.setSliderStyle(juce::Slider::LinearHorizontal);
    row.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 20);
    row.slider.setRange(0.0, maximum, 0.01);
    row.slider.textFromValueFunction = [] (double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; };
    row.slider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue() / 100.0; };
    row.slider.updateText();

    row.slider.onDragStart = [this] { roll.beginParameterEdit(); };
    row.slider.onDragEnd = [this] { roll.endParameterEdit(); };
    row.slider.onValueChange = [this, setter, &row]
    {
        if (updating)
            return;

        const auto value = static_cast<float>(row.slider.getValue());
        roll.applyToSelection([&] (model::NoteEdit& e) { setter(e, value); });
    };

    addAndMakeVisible(row.slider);
}

void NoteParamPanel::setLanguage(Language l)
{
    language = l;
    updateTexts();
    refresh();
}

void NoteParamPanel::updateTexts()
{
    const auto& s = getStrings(language);
    center.label.setText(s.paramCenter, juce::dontSendNotification);
    vibrato.label.setText(s.paramVibrato, juce::dontSendNotification);
    drift.label.setText(s.paramDrift, juce::dontSendNotification);
    transition.label.setText(s.paramTransition, juce::dontSendNotification);
    emptyHint.setText(s.paramNone, juce::dontSendNotification);
}

void NoteParamPanel::refresh()
{
    const model::Note* first = nullptr;

    if (auto* modification = roll.getModification())
        if (! roll.getSelection().empty())
            first = modification->getModel().findNote(*roll.getSelection().begin());

    const bool hasNote = first != nullptr;

    const juce::ScopedValueSetter<bool> guard(updating, true);

    for (auto* row : { &center, &vibrato, &drift, &transition })
    {
        row->slider.setEnabled(hasNote);
        row->label.setEnabled(hasNote);
    }

    emptyHint.setVisible(! hasNote);
    pitchInfo.setVisible(hasNote);

    if (hasNote)
    {
        center.slider.setValue(first->edit.centerCorrection, juce::dontSendNotification);
        vibrato.slider.setValue(first->edit.vibratoScale, juce::dontSendNotification);
        drift.slider.setValue(first->edit.driftScale, juce::dontSendNotification);
        transition.slider.setValue(first->edit.transitionSmoothness, juce::dontSendNotification);

        const float semitones = first->edit.pitchOffsetCents / 100.0f;
        pitchInfo.setText(getStrings(language).paramPitch + ": " + (semitones >= 0.0f ? "+" : "") + juce::String(semitones, 2) + " st",
                          juce::dontSendNotification);
    }
}

void NoteParamPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff20232a));
}

void NoteParamPanel::resized()
{
    auto area = getLocalBounds().reduced(10, 8);

    pitchInfo.setBounds(area.removeFromTop(22));
    emptyHint.setBounds(pitchInfo.getBounds());
    area.removeFromTop(6);

    for (auto* row : { &center, &vibrato, &drift, &transition })
    {
        row->label.setBounds(area.removeFromTop(18));
        row->slider.setBounds(area.removeFromTop(24));
        area.removeFromTop(8);
    }
}
} // namespace koenote::ui
