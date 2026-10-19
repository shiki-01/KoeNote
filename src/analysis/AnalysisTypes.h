#pragma once

#include <cstddef>
#include <functional>

namespace koenote::analysis
{
// モノラル化済みの解析対象音声への参照（所有しない）
struct MonoAudioView
{
    const float* samples = nullptr;
    std::size_t numSamples = 0;
    double sampleRate = 0.0;

    double durationSec() const noexcept { return sampleRate > 0.0 ? static_cast<double>(numSamples) / sampleRate : 0.0; }
};

// 進捗通知（0..1）。false を返すと解析を中断する。
// 解析は別スレッドで実行されるため、この callback もそのスレッドから呼ばれる。
using ProgressCallback = std::function<bool(float progress)>;
} // namespace koenote::analysis
