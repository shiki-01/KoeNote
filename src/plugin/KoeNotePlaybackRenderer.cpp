#include "plugin/KoeNotePlaybackRenderer.h"

#include "plugin/KoeNoteAudioModification.h"
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

    // ARA モデルの編集中や、レンダリング結果の差し替え中は何も読まない（無音を返す）
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

        auto* modification = static_cast<KoeNoteAudioModification*>(region->getAudioModification());
        const auto audio = modification->audioForPlayback();

        // 読み込み未完了のリージョンは鳴らさない（読み込み完了後に鳴る）
        if (! audio.valid)
        {
            success = false;
            continue;
        }

        const auto startInBlock = static_cast<int>(renderRange.getStart() - blockStart);
        const auto count = static_cast<int>(renderRange.getLength());
        const int outChannels = std::min(buffer.getNumChannels(), numChannels);

        if (std::abs(audio.sampleRate - sampleRate) <= 0.5)
        {
            // リージョン内の位置 → AudioModification 内の位置への変換。リージョンのタイムストレッチは未対応。
            const auto offsetToSource = region->getStartInAudioModificationSamples() - regionRange.getStart();

            render::accumulatePassthrough(audio.view,
                                          renderRange.getStart() + offsetToSource,
                                          buffer.getArrayOfWritePointers(),
                                          outChannels,
                                          startInBlock,
                                          count);
        }
        else
        {
            // 元音声とプロジェクトのサンプルレートが異なる場合は、線形補間で変換する（簡易的なフォールバック）
            const double startPosition = (region->getStartInAudioModificationTime()
                                          + static_cast<double>(renderRange.getStart() - regionRange.getStart()) / sampleRate)
                                         * audio.sampleRate;

            render::accumulateResampled(audio.view,
                                        startPosition,
                                        audio.sampleRate / sampleRate,
                                        buffer.getArrayOfWritePointers(),
                                        outChannels,
                                        startInBlock,
                                        count);
        }
    }

    return success;
}
} // namespace koenote::plugin
