#include "ui/Strings.h"

namespace koenote::ui
{
namespace
{
Strings makeJapanese()
{
    Strings s;
    s.toolSelect = juce::CharPointer_UTF8("選択");
    s.toolSplit = juce::CharPointer_UTF8("分割");
    s.toolBoundary = juce::CharPointer_UTF8("境界");
    s.snapOff = juce::CharPointer_UTF8("スナップなし");
    s.snapSemitone = juce::CharPointer_UTF8("半音");
    s.snapScale = juce::CharPointer_UTF8("スケール");
    s.scaleMajor = juce::CharPointer_UTF8("メジャー");
    s.scaleMinor = juce::CharPointer_UTF8("マイナー");
    s.undo = juce::CharPointer_UTF8("元に戻す");
    s.redo = juce::CharPointer_UTF8("やり直し");
    s.merge = juce::CharPointer_UTF8("結合");
    s.reset = juce::CharPointer_UTF8("リセット");
    s.zoomFit = juce::CharPointer_UTF8("全体表示");
    s.language = "English";
    s.reanalyze = juce::CharPointer_UTF8("再解析");
    s.reanalyzeTitle = juce::CharPointer_UTF8("再解析");
    s.reanalyzeMessage = juce::CharPointer_UTF8("編集内容をすべて破棄して、元の音声から解析し直します。よろしいですか？");
    s.ok = "OK";
    s.cancel = juce::CharPointer_UTF8("キャンセル");
    s.paramCenter = juce::CharPointer_UTF8("ピッチ中心の補正");
    s.paramVibrato = juce::CharPointer_UTF8("ビブラート量");
    s.paramDrift = juce::CharPointer_UTF8("ピッチドリフト");
    s.paramTransition = juce::CharPointer_UTF8("遷移の滑らかさ");
    s.paramPitch = juce::CharPointer_UTF8("ピッチ移動");
    s.paramNone = juce::CharPointer_UTF8("ノートを選択してください");
    s.statusWaiting = juce::CharPointer_UTF8("音声の取得を待っています…");
    s.statusAnalyzing = juce::CharPointer_UTF8("解析中…");
    s.statusDone = juce::CharPointer_UTF8("解析済み");
    s.statusFailed = juce::CharPointer_UTF8("解析に失敗しました（有声区間が見つかりません）");
    s.statusRendering = juce::CharPointer_UTF8("レンダリング中…");
    s.statusNoNotes = juce::CharPointer_UTF8("ノートが検出されませんでした");
    s.notARA = juce::CharPointer_UTF8("ARA 非対応のホストです。KoeNote は ARA2 対応のホストで、クリップに割り当てて使用してください。");
    s.noClip = juce::CharPointer_UTF8("編集するクリップがありません。クリップに KoeNote を割り当ててください。");
    s.notes = juce::CharPointer_UTF8("ノート");
    s.tipSelect = juce::CharPointer_UTF8("ドラッグで上下に移動してピッチを変更。Alt+ドラッグで時間を移動。端をドラッグで長さを変更。ダブルクリックでリセット。");
    s.tipSplit = juce::CharPointer_UTF8("ノートをクリックした位置で分割します。");
    s.tipBoundary = juce::CharPointer_UTF8("隣り合うノートの境界をドラッグして移動します。");
    s.tipMerge = juce::CharPointer_UTF8("選択したノートと直後のノートを結合します (J)。");
    s.tipReset = juce::CharPointer_UTF8("選択したノートの編集内容を元に戻します (Delete)。");
    s.tipReanalyze = juce::CharPointer_UTF8("編集内容を破棄して、音声を解析し直します。");
    return s;
}

Strings makeEnglish()
{
    Strings s;
    s.toolSelect = "Select";
    s.toolSplit = "Split";
    s.toolBoundary = "Boundary";
    s.snapOff = "No snap";
    s.snapSemitone = "Semitone";
    s.snapScale = "Scale";
    s.scaleMajor = "Major";
    s.scaleMinor = "Minor";
    s.undo = "Undo";
    s.redo = "Redo";
    s.merge = "Merge";
    s.reset = "Reset";
    s.zoomFit = "Fit";
    s.language = juce::CharPointer_UTF8("日本語");
    s.reanalyze = "Re-analyze";
    s.reanalyzeTitle = "Re-analyze";
    s.reanalyzeMessage = "All edits will be discarded and the audio will be analyzed again. Continue?";
    s.ok = "OK";
    s.cancel = "Cancel";
    s.paramCenter = "Pitch center correction";
    s.paramVibrato = "Vibrato amount";
    s.paramDrift = "Pitch drift";
    s.paramTransition = "Transition smoothness";
    s.paramPitch = "Pitch shift";
    s.paramNone = "Select a note";
    s.statusWaiting = "Waiting for audio from the host...";
    s.statusAnalyzing = "Analyzing...";
    s.statusDone = "Analysis done";
    s.statusFailed = "Analysis failed (no voiced segments found)";
    s.statusRendering = "Rendering...";
    s.statusNoNotes = "No notes were detected";
    s.notARA = "This host does not support ARA. Use KoeNote in an ARA2 host by assigning it to a clip.";
    s.noClip = "No clip to edit. Assign KoeNote to a clip.";
    s.notes = "notes";
    s.tipSelect = "Drag vertically to change pitch. Alt+drag to move in time. Drag an edge to change length. Double-click to reset.";
    s.tipSplit = "Click a note to split it at that position.";
    s.tipBoundary = "Drag the boundary between adjacent notes.";
    s.tipMerge = "Merge the selected note with the next one (J).";
    s.tipReset = "Reset the selected notes (Delete).";
    s.tipReanalyze = "Discard all edits and analyze the audio again.";
    return s;
}
} // namespace

const Strings& getStrings(Language language)
{
    static const Strings japanese = makeJapanese();
    static const Strings english = makeEnglish();
    return language == Language::Japanese ? japanese : english;
}

Language detectDefaultLanguage()
{
    return juce::SystemStats::getUserLanguage().startsWithIgnoreCase("ja") ? Language::Japanese : Language::English;
}
} // namespace koenote::ui
