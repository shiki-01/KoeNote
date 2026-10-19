#pragma once

#include "analysis/AnalysisTypes.h"
#include "model/Note.h"
#include "model/PitchTrack.h"

#include <vector>

namespace koenote::analysis
{
// F0 推定の interface。実装はオーディオスレッドから呼んではならない。
// analyze は同期関数だが、呼び出し側（AnalysisScheduler 等）が別スレッドで実行する前提で、
// ProgressCallback による中断に対応すること。
class IPitchAnalyzer
{
public:
    virtual ~IPitchAnalyzer() = default;

    // 中断された場合は空の PitchTrack を返す
    virtual model::PitchTrack analyze(const MonoAudioView& audio, const ProgressCallback& progress) = 0;
};

// F0 からのノート分割の interface
class INoteSegmenter
{
public:
    virtual ~INoteSegmenter() = default;

    // 戻り値のノートは startSec 昇順・重なりなし。id は呼び出し側で採番するため 0 のままでよい。
    virtual std::vector<model::Note> segment(const model::PitchTrack& track, const MonoAudioView& audio) = 0;
};
} // namespace koenote::analysis
