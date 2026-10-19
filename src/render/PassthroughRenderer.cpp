#include "render/PassthroughRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace koenote::render
{
namespace
{
// 窓付き sinc のカーネル（Blackman 窓、片側 kHalfTaps サンプル）。オーディオスレッドで sin を計算しないよう、
// 表を静的初期化で作り、線形補間で引く。
constexpr int kHalfTaps = 16;
constexpr int kOversample = 512;
constexpr double kMaxKernelScale = 4.0; // ダウンサンプルの比がこれを超える場合は、ローパスが不十分になる（192 kHz → 44.1 kHz 程度まで）
constexpr int kMaxTaps = 2 * kHalfTaps * 4 + 2;

constexpr double kPi = 3.14159265358979323846;

struct KernelTable
{
    std::array<float, kHalfTaps * kOversample + 2> values {};

    KernelTable()
    {
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            const double t = static_cast<double>(i) / kOversample; // 0..kHalfTaps
            if (t >= kHalfTaps)
                continue;

            // 整数の位置は正確に 0（step == 1 で元の音声と一致させる）
            const bool integer = i % kOversample == 0;
            const double sinc = t == 0.0 ? 1.0 : (integer ? 0.0 : std::sin(kPi * t) / (kPi * t));
            const double w = t / kHalfTaps; // 0..1
            const double window = 0.42 + 0.5 * std::cos(kPi * w) + 0.08 * std::cos(2.0 * kPi * w);
            values[i] = static_cast<float>(sinc * window);
        }
    }
};

const KernelTable kKernelTable;

// t は入力サンプル単位の距離（カットオフで正規化済み）。|t| >= kHalfTaps では 0。
float kernel(double t) noexcept
{
    const double a = std::abs(t) * kOversample;
    const auto i = static_cast<std::size_t>(a);

    if (i + 1 >= kKernelTable.values.size())
        return 0.0f;

    const auto fraction = static_cast<float>(a - static_cast<double>(i));
    return kKernelTable.values[i] + (kKernelTable.values[i + 1] - kKernelTable.values[i]) * fraction;
}
} // namespace

void accumulatePassthrough(const AudioView& src,
                           std::int64_t srcStart,
                           float* const* dst,
                           int numDstChannels,
                           int dstOffset,
                           int numFrames) noexcept
{
    if (src.channels == nullptr || src.numChannels <= 0 || numFrames <= 0 || numDstChannels <= 0)
        return;

    // src 上で有効な範囲に絞る
    const std::int64_t validBegin = std::max<std::int64_t>(0, -srcStart);
    const std::int64_t validEnd = std::min<std::int64_t>(numFrames, src.numFrames - srcStart);

    if (validEnd <= validBegin)
        return;

    const bool downmix = numDstChannels == 1 && src.numChannels > 1;
    const float downmixGain = 1.0f / static_cast<float>(src.numChannels);

    for (int c = 0; c < numDstChannels; ++c)
    {
        float* out = dst[c] + dstOffset;

        if (downmix)
        {
            for (std::int64_t i = validBegin; i < validEnd; ++i)
            {
                float sum = 0.0f;
                for (int s = 0; s < src.numChannels; ++s)
                    sum += src.channels[s][srcStart + i];
                out[i] += sum * downmixGain;
            }
        }
        else
        {
            const float* in = src.channels[std::min(c, src.numChannels - 1)];

            for (std::int64_t i = validBegin; i < validEnd; ++i)
                out[i] += in[srcStart + i];
        }
    }
}
void accumulateResampled(const AudioView& src,
                         double srcStartPosition,
                         double step,
                         float* const* dst,
                         int numDstChannels,
                         int dstOffset,
                         int numFrames) noexcept
{
    if (src.channels == nullptr || src.numChannels <= 0 || numFrames <= 0 || numDstChannels <= 0 || ! (step > 0.0))
        return;

    const bool downmix = numDstChannels == 1 && src.numChannels > 1;
    const float downmixGain = 1.0f / static_cast<float>(src.numChannels);
    const auto last = src.numFrames - 1;

    // ダウンサンプル（step > 1）では、カーネルを step 倍に広げて出力の Nyquist でローパスする
    const double scale = std::clamp(step, 1.0, kMaxKernelScale);
    const double cutoff = 1.0 / scale;
    const double halfWidth = static_cast<double>(kHalfTaps) * scale; // 入力サンプル単位

    std::array<float, kMaxTaps> weights {};

    for (int i = 0; i < numFrames; ++i)
    {
        const double position = srcStartPosition + static_cast<double>(i) * step;
        const auto index = static_cast<std::int64_t>(std::floor(position));

        if (index < 0 || index > last)
            continue;

        // 有効な重みの範囲 [first, first + count)（範囲外のサンプルは無音として扱う）
        const auto first = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(position - halfWidth)) + 1);
        const auto end = std::min<std::int64_t>(last + 1, static_cast<std::int64_t>(std::floor(position + halfWidth)) + 1);
        const int count = static_cast<int>(std::min<std::int64_t>(end - first, kMaxTaps));

        for (int k = 0; k < count; ++k)
            weights[static_cast<std::size_t>(k)] = static_cast<float>(cutoff) * kernel((position - static_cast<double>(first + k)) * cutoff);

        const auto sample = [&] (int channel)
        {
            const float* in = src.channels[channel] + first;
            float sum = 0.0f;

            for (int k = 0; k < count; ++k)
                sum += in[k] * weights[static_cast<std::size_t>(k)];

            return sum;
        };

        for (int c = 0; c < numDstChannels; ++c)
        {
            float value = 0.0f;

            if (downmix)
            {
                for (int s = 0; s < src.numChannels; ++s)
                    value += sample(s);
                value *= downmixGain;
            }
            else
            {
                value = sample(std::min(c, src.numChannels - 1));
            }

            dst[c][dstOffset + i] += value;
        }
    }
}
} // namespace koenote::render
