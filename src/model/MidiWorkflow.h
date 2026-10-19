#pragma once

#include "midi/SmfIO.h"
#include "model/EditModel.h"

#include <utility>
#include <vector>

namespace koenote::model
{
// MIDI 抽出（F-11）・MIDI をターゲットに適用（F-12）の、モデルと SMF の間の変換。UI から使う純粋な関数。

// 解析ノートの MIDI 化。edited == false は元音源のノート（元の区間・検出した音高）、true は編集後（出力時間軸・目標音高）。
// 音高は四捨五入、ベロシティはノートの音量（RMS）から 20..127 へ写す。
std::vector<midi::MidiNote> notesToMidi(const std::vector<Note>& notes, bool edited);

// 元音源のノートの区間内の、ピッチ曲線（ノート中心からの偏差、半音）。pitch bend として書き出す用。
std::vector<midi::PitchBendPoint> pitchBendCurve(const PitchTrack& track, const std::vector<Note>& notes);

// MIDI ノートを解析ノートへ対応付けた結果。targets は offsetSec を引いた後の時刻。
struct MidiImport
{
    std::vector<TargetNote> targets;
    std::vector<std::pair<NoteId, int>> assignments;
    int unmatchedAnalysis = 0; // 対応付けできなかった解析ノートの数
    int unmatchedTargets = 0;  // 対応付けできなかった MIDI ノートの数
};

// MIDI の先頭のノートを、解析ノートの先頭に合わせるためのオフセット（秒）。引いて使う（targets = midi - offset）。
// どちらかが空なら 0。
double autoMidiOffset(const std::vector<Note>& notes, const midi::MidiSequence& sequence);

// sequence の各ノートの時刻から offsetSec を引いて目標とし、解析ノートと対応付ける（元の区間・検出した音高で比較）。
MidiImport importMidi(const std::vector<Note>& notes, const midi::MidiSequence& sequence, double offsetSec);

// 割り当て済みのノートへ、目標のピッチ・タイミングを strength（0..1）の割合で適用する編集を計算する。
// 割り当てのないノートは含まれない。戻り値は EditModel::applyEdits に渡す。
std::vector<std::pair<NoteId, NoteEdit>> computeTargetEdits(const EditModel& model, float pitchStrength, float timingStrength);
} // namespace koenote::model
