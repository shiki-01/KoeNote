#include "plugin/KoeNotePlaybackRenderer.h"

#include "plugin/KoeNoteAudioSource.h"
#include "render/PassthroughRenderer.h"

#include <algorithm>
#include <cmath>

namespace koenote::plugin
{
void KoeNotePlaybackRenderer::prepareToPlay(double sampleRateIn,
                                            int maximumSamplesPerBlock,
                                            int numChannelsIn,
                                            juce::AudioProcessor::ProcessingPrecision precision,
                                            AlwaysNonRealtime alwaysNonRealtime)
{
    sampleRate = sampleRateIn;
    numChannels = numChannelsIn;
    juce::ARAPlaybackRenderer::prepareToPlay(sampleRateIn, maximumSamplesPerBlock, numChannelsIn, precision, alwaysNonRealtime);
}

void KoeNotePlaybackRenderer::releaseResources()
{
    juce::ARAPlaybackRenderer::releaseResources();
}

bool KoeNotePlaybackRenderer::processBlock(juce::AudioBuffer<float>& buffer,
                                           juce::AudioProcessor::Realtime,
                                           const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept
{
    buffer.clear();

    // ARA モデルの編集中は何も読まない（無音を返す）
    const auto scope = gate.tryRead();
    if (! scope.locked())
        return true;

    if (! positionInfo.getIsPlaying())
        return true;

    const auto numSamples = buffer.getNumSamples();
    const auto blockStart = positionInfo.getTimeInSamples().orFallback(0);
    const auto blockRange = juce::Range<juce::int64>::withStartAndLength(blockStart, numSamples);

    bool success = true;

    for (const auto& region : getPlaybackRegions())
    {
        // 再生位置（プロジェクト時間）上のリージョン範囲と、このブロックの交差部分
        const auto regionRange = region->getSampleRange(sampleRate, juce::ARAPlaybackRegion::IncludeHeadAndTail::no);
        const auto renderRange = blockRange.getIntersectionWith(regionRange);

        if (renderRange.isEmpty())
            continue;

        auto* source = static_cast<KoeNoteAudioSource*>(region->getAudioModification()->getAudioSource());
        const auto* audio = source->audioForPlayback();

        // 読み込み未完了、またはサンプルレートが異なる場合はこのリージョンを鳴らさない
        // （サンプルレート変換はフェーズ1 で対応する）
        if (audio == nullptr || std::abs(audio->sampleRate - sampleRate) > 0.5)
        {
            success = false;
            continue;
        }

        // リージョン内の位置 → 元音声（AudioModification）内の位置への変換。タイムストレッチなし。
        const auto modificationStart = region->getStartInAudioModificationSamples();
        const auto offsetToSource = modificationStart - regionRange.getStart();

        const auto startInBlock = static_cast<int>(renderRange.getStart() - blockStart);
        const auto count = static_cast<int>(renderRange.getLength());

        render::accumulatePassthrough(audio->view(),
                                      renderRange.getStart() + offsetToSource,
                                      buffer.getArrayOfWritePointers(),
                                      std::min(buffer.getNumChannels(), numChannels),
                                      startInBlock,
                                      count);
    }

    return success;
}
} // namespace koenote::plugin
