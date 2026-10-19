#pragma once

#include "render/PassthroughRenderer.h"

#include <cstdint>
#include <vector>

namespace koenote::plugin
{
// ホストから取得した元音声の全サンプル（非インターリーブ float）。
struct SourceAudioData
{
    int numChannels = 0;
    std::int64_t numFrames = 0;
    double sampleRate = 0.0;
    std::vector<std::vector<float>> channels;
    std::vector<const float*> channelPointers;

    render::AudioView view() const noexcept { return { channelPointers.data(), numChannels, numFrames }; }
};
} // namespace koenote::plugin
