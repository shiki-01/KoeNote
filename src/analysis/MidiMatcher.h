#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace koenote::analysis
{
// 時刻と音高を持つノート（秒・MIDI ノート番号）
struct TimedNote
{
    double startSec = 0.0;
    double endSec = 0.0;
    float midi = 60.0f;
};

struct MatchOptions
{
    double maxOnsetDiffSec = 0.5;        // これを超えて開始時刻が離れたペアは対応付けしない
    double maxPitchDiffSemitones = 3.0;  // これを超えて音高が離れたペアは対応付けしない（オクターブ違いも許容しない）
    double timeWeight = 1.0;             // 開始・終了の時間差（秒）に掛ける係数
    double pitchWeight = 0.5;            // 音高差（半音）に掛ける係数
    double skipCost = 1.0;               // 対応付けしない（スキップ）ときのコスト
};

struct MatchResult
{
    std::vector<std::pair<int, int>> pairs; // (analysisIndex, targetIndex)。時間順で単調
    std::vector<int> unmatchedAnalysis;     // 対応付けなしの解析側の添字（昇順）
    std::vector<int> unmatchedTarget;       // 対応付けなしのターゲット側の添字（昇順）
};

// 解析ノートと MIDI ノートを Needleman-Wunsch 型の動的計画法で対応付ける（要件 F-12）。
// 添字は入力ベクトルの元の位置を指す。入力が startSec 昇順でなくても内部でソートする。
MatchResult matchNotes(const std::vector<TimedNote>& analysed,
                       const std::vector<TimedNote>& target,
                       const MatchOptions& options = {});
} // namespace koenote::analysis
