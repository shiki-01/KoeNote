#pragma once

#include <vector>

namespace koenote::analysis
{
// キー候補 1 件（要件 F-15）
struct KeyCandidate
{
    int root = 0;          // 0 = C .. 11 = B
    bool minor = false;
    float score = 0.0f;    // Krumhansl-Schmuckler のピアソン相関係数（-1..1）
};

struct KeyResult
{
    int root = 0;          // 0 = C .. 11 = B
    bool minor = false;
    float confidence = 0.0f; // 0..1。1 位と 2 位の相関の差から算出する
    bool valid = false;      // 入力が不足・退化している場合は false
    std::vector<KeyCandidate> ranking; // 上位 5 件（score 降順）。valid が false の場合は空
};

// キー推定に使うノート情報
struct NoteForKey
{
    float midi = 60.0f;   // MIDI ノート番号（小数可）
    double durationSec = 0.0;
};

// Krumhansl-Schmuckler 法によるキー・スケール検出
KeyResult detectKey(const std::vector<NoteForKey>& notes);
} // namespace koenote::analysis
