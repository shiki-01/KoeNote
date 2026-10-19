#pragma once

#include "analysis/AnalysisTypes.h"

#include <vector>

namespace koenote::analysis
{
// 対応する時刻の組（guide 側の時刻と target 側の時刻）
struct AlignmentAnchor
{
    double guideSec = 0.0;
    double targetSec = 0.0;
};

// DTW による時間軸の対応付け結果。
// path は targetSec・guideSec ともに単調非減少で、約 50 ms 間隔に間引かれている（端点を含む）。
struct AlignmentResult
{
    bool valid = false;
    std::vector<AlignmentAnchor> path;

    // target 上の時刻を guide 上の時刻へ写す（path の線形補間）。範囲外は端の傾き 1 で外挿する。
    // valid == false のときは入力をそのまま返す。
    double mapTargetToGuide(double targetSec) const;
    // guide 上の時刻を target 上の時刻へ写す。範囲外の扱いは mapTargetToGuide と同じ。
    double mapGuideToTarget(double guideSec) const;
};

struct AlignmentOptions
{
    double hopSec = 0.01;
    int numMfcc = 13;
    int numMelBands = 26;
    double bandRadiusSec = 6.0;      // Sakoe-Chiba 帯の半径（秒）。メモリは O(フレーム数 x 帯幅)
    double maxSampleRate = 16000.0;  // これを超える音声は整数倍で間引いて特徴量を計算する
};

// guide と target の MFCC 系列を帯付き DTW で整列する（要件 F-13）。
// 入力が不正・極端に短い・progress が false を返した場合は valid = false を返す。
AlignmentResult alignAudio(const MonoAudioView& guide, const MonoAudioView& target,
                           const AlignmentOptions& options = {}, const ProgressCallback& progress = {});
} // namespace koenote::analysis
