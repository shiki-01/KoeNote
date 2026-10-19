#pragma once

#include "ui/PianoRollView.h"
#include "ui/SliderRow.h"
#include "ui/Strings.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace koenote::ui
{
// 選択中のノートの編集パラメータを操作するパネル（F-04・F-05・F-09・F-10）。
// 複数選択時は、最初のノートの値を表示し、変更は選択中の全ノートへ適用する。
class NoteParamPanel final : public juce::Component
{
public:
    explicit NoteParamPanel(PianoRollView& view);

    void setLanguage(Language l);
    // 選択やモデルが変わったときに呼ぶ
    void refresh();

    // 全項目を表示するのに必要な高さ
    int getPreferredHeight() const noexcept;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void setupRow(SliderRow& row, double minimum, double maximum, double step, double resetValue, SliderRow::Format format,
                  std::function<void(model::NoteEdit&, float)> setter);
    void updateTexts();

    PianoRollView& roll;
    Language language = Language::Japanese;
    bool updating = false;

    SliderRow center, vibrato, drift, transition;
    SliderRow gain, formant, formantFollow, consonant, breath;
    juce::Label pitchInfo;
    juce::Label emptyHint;
    juce::Label pitchSection, formantSection, gainSection;
};
} // namespace koenote::ui
