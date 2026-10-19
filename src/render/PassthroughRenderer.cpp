#include "render/PassthroughRenderer.h"

#include <algorithm>

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
} // namespace koenote::render
