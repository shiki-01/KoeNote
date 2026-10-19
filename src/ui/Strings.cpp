#include "ui/Strings.h"

namespace koenote::ui
{
namespace
{
// 日本語の文字列は UTF-8 のリテラルを String に変換する
juce::String ja(const char* utf8) { return juce::String::fromUTF8(utf8); }

Strings makeJapanese()
{
    Strings s;
    s.toolSelect = ja("選択");
    s.toolSplit = ja("分割");
    s.toolBoundary = ja("境界");
    s.toolAssign = ja("割り当て");
    s.snapOff = ja("スナップなし");
    s.snapSemitone = ja("半音");
    s.snapScale = ja("スケール");
    s.scaleMajor = ja("メジャー");
    s.scaleMinor = ja("マイナー");
    s.undo = ja("元に戻す");
    s.redo = ja("やり直し");
    s.merge = ja("結合");
    s.reset = ja("リセット");
    s.zoomFit = ja("全体表示");
    s.language = "English";
    s.reanalyze = ja("再解析");
    s.reanalyzeTitle = ja("再解析");
    s.reanalyzeMessage = ja("編集内容をすべて破棄して、元の音声から解析し直します。よろしいですか？");
    s.ok = "OK";
    s.cancel = ja("キャンセル");
    s.abButton = "A/B";

    s.tabNote = ja("ノート");
    s.tabBatch = ja("一括補正");
    s.tabMidi = "MIDI";
    s.tabMore = ja("その他");

    s.paramCenter = ja("ピッチ中心の補正");
    s.paramVibrato = ja("ビブラート量");
    s.paramDrift = ja("ピッチドリフト");
    s.paramTransition = ja("遷移の滑らかさ");
    s.paramPitch = ja("ピッチ移動");
    s.paramNone = ja("ノートを選択してください");
    s.paramGain = ja("ゲイン");
    s.paramFormant = ja("フォルマントシフト");
    s.paramFormantFollow = ja("フォルマントのピッチ追従");
    s.paramConsonant = ja("子音のゲイン（直前）");
    s.paramBreath = ja("ブレスのゲイン（直後）");

    s.batchTitle = ja("一括補正");
    s.batchPitchStrength = ja("ピッチ補正の強さ");
    s.batchVibratoKeep = ja("ビブラートの保持");
    s.batchTimingStrength = ja("クオンタイズの強さ");
    s.batchGrid = ja("グリッド");
    s.batchKeepDuration = ja("ノートの長さを保つ");
    s.batchBpm = ja("BPM（ホストのテンポがないとき）");
    s.batchApplySelected = ja("選択ノートに適用");
    s.batchApplyAll = ja("全ノートに適用");
    s.batchDetectKey = ja("キー・スケールを検出");
    s.batchKeyResult = ja("検出したキー: ");
    s.batchKeyNone = ja("キーを検出できませんでした");
    s.batchNoTempo = ja("テンポ情報がありません。BPM を入力してください。");
    s.grid4 = ja("4 分音符");
    s.grid8 = ja("8 分音符");
    s.grid16 = ja("16 分音符");
    s.grid32 = ja("32 分音符");

    s.midiExportTitle = ja("MIDI の抽出");
    s.midiDrag = ja("ここからドラッグして DAW へ");
    s.midiSave = ja("保存…");
    s.midiWithBend = ja("ピッチベンドを含める");
    s.midiEdited = ja("編集後のノートを書き出す");
    s.midiDragHint = ja("MIDI トラックへドロップすると、元音源のノートが取り込まれます。");
    s.midiImportTitle = ja("MIDI をターゲットに適用");
    s.midiLoad = ja("読み込み…");
    s.midiOffset = ja("MIDI の時刻のずれ（秒）");
    s.midiAutoOffset = ja("先頭を合わせる");
    s.midiPitchStrength = ja("ピッチの適用強度");
    s.midiTimingStrength = ja("タイミングの適用強度");
    s.midiApply = ja("適用");
    s.midiClear = ja("目標を解除");
    s.midiStatus = ja("目標 %t ノート / 対応 %p / 未対応（解析側）%a / 未対応（MIDI 側）%m");
    s.midiDropHint = ja("MIDI ファイルをウィンドウへドロップしても読み込めます。");
    s.midiAssignHint = ja("未対応のノートは、「割り当て」ツールでノートを選び、目標（枠線）をクリックして対応付けます。");
    s.midiLoadFailed = ja("MIDI ファイルを読み込めませんでした。");
    s.midiSaveFailed = ja("MIDI ファイルを保存できませんでした。");
    s.midiNoNotes = ja("書き出すノートがありません。");

    s.compareTitle = ja("A/B 比較");
    s.compareOriginal = ja("元の音声で聴く（バイパス）");
    s.compareRange = ja("選択ノートの範囲だけ");
    s.alignTitle = ja("タイミング整列");
    s.alignGuide = ja("ガイド（リードボーカルなど）");
    s.alignRun = ja("ガイドに整列");
    s.alignFile = ja("ファイルから…");
    s.alignNoGuide = ja("ガイドにできる音声がありません");
    s.alignWorking = ja("整列中…");
    s.alignDone = ja("整列しました");
    s.alignFailed = ja("整列できませんでした");
    s.neuralTitle = ja("ニューラル加工（高品質）");
    s.neuralEnable = ja("ニューラルボコーダーで合成する");
    s.neuralUnavailable = ja("利用できません: ");
    s.neuralRendering = ja("合成中…");
    s.neuralReady = ja("反映済み");
    s.neuralFailed = ja("失敗: ");
    s.neuralFolderHint = ja("ボコーダーのモデルと onnxruntime.dll を、次のフォルダへ配置します: ");
    s.neuralWaiting = ja("待機中（編集があると合成します）");

    s.statusWaiting = ja("音声の取得を待っています…");
    s.statusAnalyzing = ja("解析中…");
    s.statusDone = ja("解析済み");
    s.statusFailed = ja("解析に失敗しました（有声区間が見つかりません）");
    s.statusRendering = ja("レンダリング中…");
    s.statusNoNotes = ja("ノートが検出されませんでした");
    s.notARA = ja("ARA 非対応のホストです。KoeNote は ARA2 対応のホストで、クリップに割り当てて使用してください。");
    s.noClip = ja("編集するクリップがありません。クリップに KoeNote を割り当ててください。");
    s.notes = ja("ノート");

    s.tipSelect = ja("ドラッグで上下に移動してピッチを変更。Alt+ドラッグで時間を移動。端をドラッグで長さを変更。ダブルクリックでリセット。");
    s.tipSplit = ja("ノートをクリックした位置で分割します。");
    s.tipBoundary = ja("隣り合うノートの境界をドラッグして移動します。");
    s.tipAssign = ja("ノートを選択し、MIDI の目標（枠線）をクリックして対応付けます。");
    s.tipMerge = ja("選択したノートと直後のノートを結合します (J)。");
    s.tipReset = ja("選択したノートの編集内容を元に戻します (Delete)。");
    s.tipReanalyze = ja("編集内容を破棄して、音声を解析し直します。");
    s.tipAB = ja("編集前の音声と聴き比べます。");
    return s;
}

Strings makeEnglish()
{
    Strings s;
    s.toolSelect = "Select";
    s.toolSplit = "Split";
    s.toolBoundary = "Boundary";
    s.toolAssign = "Assign";
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
    s.language = ja("日本語");
    s.reanalyze = "Re-analyze";
    s.reanalyzeTitle = "Re-analyze";
    s.reanalyzeMessage = "All edits will be discarded and the audio will be analyzed again. Continue?";
    s.ok = "OK";
    s.cancel = "Cancel";
    s.abButton = "A/B";

    s.tabNote = "Note";
    s.tabBatch = "Batch";
    s.tabMidi = "MIDI";
    s.tabMore = "More";

    s.paramCenter = "Pitch center correction";
    s.paramVibrato = "Vibrato amount";
    s.paramDrift = "Pitch drift";
    s.paramTransition = "Transition smoothness";
    s.paramPitch = "Pitch shift";
    s.paramNone = "Select a note";
    s.paramGain = "Gain";
    s.paramFormant = "Formant shift";
    s.paramFormantFollow = "Formant follows pitch";
    s.paramConsonant = "Consonant gain (before)";
    s.paramBreath = "Breath gain (after)";

    s.batchTitle = "Batch correction";
    s.batchPitchStrength = "Pitch correction strength";
    s.batchVibratoKeep = "Vibrato kept";
    s.batchTimingStrength = "Quantize strength";
    s.batchGrid = "Grid";
    s.batchKeepDuration = "Keep note length";
    s.batchBpm = "BPM (when the host gives no tempo)";
    s.batchApplySelected = "Apply to selected";
    s.batchApplyAll = "Apply to all notes";
    s.batchDetectKey = "Detect key / scale";
    s.batchKeyResult = "Detected key: ";
    s.batchKeyNone = "Could not detect a key";
    s.batchNoTempo = "No tempo information. Enter a BPM.";
    s.grid4 = "1/4 note";
    s.grid8 = "1/8 note";
    s.grid16 = "1/16 note";
    s.grid32 = "1/32 note";

    s.midiExportTitle = "Extract MIDI";
    s.midiDrag = "Drag from here to the DAW";
    s.midiSave = "Save...";
    s.midiWithBend = "Include pitch bend";
    s.midiEdited = "Export edited notes";
    s.midiDragHint = "Drop onto a MIDI track to import the notes of the original audio.";
    s.midiImportTitle = "Apply MIDI as target";
    s.midiLoad = "Load...";
    s.midiOffset = "MIDI time offset (s)";
    s.midiAutoOffset = "Align start";
    s.midiPitchStrength = "Pitch strength";
    s.midiTimingStrength = "Timing strength";
    s.midiApply = "Apply";
    s.midiClear = "Clear targets";
    s.midiStatus = "%t targets / %p matched / %a unmatched (audio) / %m unmatched (MIDI)";
    s.midiDropHint = "You can also drop a MIDI file onto the window.";
    s.midiAssignHint = "To assign unmatched notes, use the Assign tool: select a note, then click a target (outline).";
    s.midiLoadFailed = "Could not read the MIDI file.";
    s.midiSaveFailed = "Could not save the MIDI file.";
    s.midiNoNotes = "There are no notes to export.";

    s.compareTitle = "A/B compare";
    s.compareOriginal = "Listen to the original (bypass)";
    s.compareRange = "Selected notes' range only";
    s.alignTitle = "Timing alignment";
    s.alignGuide = "Guide (e.g. lead vocal)";
    s.alignRun = "Align to guide";
    s.alignFile = "From file...";
    s.alignNoGuide = "No audio available as a guide";
    s.alignWorking = "Aligning...";
    s.alignDone = "Aligned";
    s.alignFailed = "Alignment failed";
    s.neuralTitle = "Neural processing (high quality)";
    s.neuralEnable = "Synthesize with a neural vocoder";
    s.neuralUnavailable = "Not available: ";
    s.neuralRendering = "Rendering...";
    s.neuralReady = "Applied";
    s.neuralFailed = "Failed: ";
    s.neuralFolderHint = "Place the vocoder model and onnxruntime.dll in: ";
    s.neuralWaiting = "Waiting (renders when there are edits)";

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
    s.tipAssign = "Select a note, then click a MIDI target (outline) to assign it.";
    s.tipMerge = "Merge the selected note with the next one (J).";
    s.tipReset = "Reset the selected notes (Delete).";
    s.tipReanalyze = "Discard all edits and analyze the audio again.";
    s.tipAB = "Compare with the audio before editing.";
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
