#pragma once

#include "model/PitchTrack.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace koenote::render
{
// TD-PSOLA 用の分析ピッチマーク。音声全体を隙間なく覆う（有声区間は波形のピークに、
// 無声区間は一定間隔に置く）。
struct PitchMark
{
    std::int64_t position = 0; // 元音声上のサンプル位置
    bool voiced = false;
};

struct PitchMarkOptions
{
    double unvoicedPeriodSec = 0.005; // 無声区間のマーク間隔
    double snapRatio = 0.25;          // 期待位置から探索する範囲（周期に対する比）
};

// mono: モノラル化した音声。F0 は track から得る。
// 戻り値は position 昇順で、先頭は 0、末尾は numSamples 付近。numSamples == 0 なら空。
std::vector<PitchMark> findPitchMarks(const float* mono,
                                      std::size_t numSamples,
                                      double sampleRate,
                                      const model::PitchTrack& track,
                                      const PitchMarkOptions& options = {});
} // namespace koenote::render
