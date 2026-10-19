#pragma once

#include <juce_core/juce_core.h>

namespace koenote::ui
{
// UI 文字列（日本語・英語。非機能要件「操作性」）
enum class Language
{
    Japanese,
    English
};

struct Strings
{
    // ツールバー
    juce::String toolSelect, toolSplit, toolBoundary, toolAssign;
    juce::String snapOff, snapSemitone, snapScale;
    juce::String scaleMajor, scaleMinor;
    juce::String undo, redo, merge, reset, zoomFit, language, reanalyze;
    juce::String reanalyzeTitle, reanalyzeMessage, ok, cancel;
    juce::String abButton;

    // タブ
    juce::String tabNote, tabBatch, tabMidi, tabMore;

    // ノートのパラメータ
    juce::String paramCenter, paramVibrato, paramDrift, paramTransition, paramPitch, paramNone;
    juce::String paramGain, paramFormant, paramFormantFollow, paramConsonant, paramBreath;

    // 一括補正
    juce::String batchTitle, batchPitchStrength, batchVibratoKeep, batchTimingStrength, batchGrid, batchKeepDuration;
    juce::String batchBpm, batchApplySelected, batchApplyAll, batchDetectKey, batchKeyResult, batchKeyNone, batchNoTempo;
    juce::String grid4, grid8, grid16, grid32;

    // MIDI
    juce::String midiExportTitle, midiDrag, midiSave, midiWithBend, midiEdited, midiDragHint;
    juce::String midiImportTitle, midiLoad, midiOffset, midiAutoOffset, midiPitchStrength, midiTimingStrength;
    juce::String midiApply, midiClear, midiStatus, midiDropHint, midiAssignHint, midiLoadFailed, midiSaveFailed, midiNoNotes;

    // その他
    juce::String compareTitle, compareOriginal, compareRange;
    juce::String alignTitle, alignGuide, alignRun, alignFile, alignNoGuide, alignWorking, alignDone, alignFailed;
    juce::String neuralTitle, neuralEnable, neuralUnavailable, neuralRendering, neuralReady, neuralFailed, neuralFolderHint, neuralWaiting;

    // 状態表示
    juce::String statusWaiting, statusAnalyzing, statusDone, statusFailed, statusRendering, statusNoNotes;
    juce::String notARA, noClip, notes;

    // ヒント
    juce::String tipSelect, tipSplit, tipBoundary, tipAssign, tipMerge, tipReset, tipReanalyze, tipAB;
};

const Strings& getStrings(Language language);
Language detectDefaultLanguage();
} // namespace koenote::ui
