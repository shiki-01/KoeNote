#pragma once

#include "ui/PianoRollView.h"
#include "ui/Strings.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace koenote::ui
{
// 選択中のノートの編集パラメータ（F-04・F-05）を操作するパネル。
// 複数選択時は、最初のノートの値を表示し、変更は選択中の全ノートへ適用する。
class NoteParamPanel final : public juce::Component
{
public:
    explicit NoteParamPanel(PianoRollView& view);

    void setLanguage(Language l);
    // 選択やモデルが変わったときに呼ぶ
    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Row
    {
        juce::Label label;
        juce::Slider slider;
    };

    void setupRow(Row& row, double maximum, std::function<void(model::NoteEdit&, float)> setter);
    void updateTexts();

    PianoRollView& roll;
    Language language = Language::Japanese;
    bool updating = false;

    Row center, vibrato, drift, transition;
    juce::Label pitchInfo;
    juce::Label emptyHint;
};
} // namespace koenote::ui
