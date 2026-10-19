#include "render/PassthroughRenderer.h"

#include <algorithm>
#include <cmath>

namespace koenote::render
{
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

    for (int i = 0; i < numFrames; ++i)
    {
        const double position = srcStartPosition + static_cast<double>(i) * step;
        const auto index = static_cast<std::int64_t>(std::floor(position));

        if (index < 0 || index > last)
            continue;

        const auto next = std::min(index + 1, last);
        const auto fraction = static_cast<float>(position - static_cast<double>(index));

        const auto sample = [&] (int channel)
        {
            const float a = src.channels[channel][index];
            const float b = src.channels[channel][next];
            return a + (b - a) * fraction;
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
