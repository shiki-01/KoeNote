#include "ui/KoeNoteEditor.h"

namespace koenote::ui
{
KoeNoteEditor::KoeNoteEditor(juce::AudioProcessor& processor)
    : juce::AudioProcessorEditor(&processor), juce::AudioProcessorEditorARAExtension(&processor)
{
    setResizable(true, false);
    setSize(800, 320);
}

void KoeNoteEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(16.0f));

    const auto text = isARAEditorView() ? juce::String("KoeNote (ARA 接続中)")
                                        : juce::String("ARA 非対応のホストです。KoeNote は ARA2 対応ホストで使用してください。");
    g.drawFittedText(text, getLocalBounds(), juce::Justification::centred, 2);
}
} // namespace koenote::ui
