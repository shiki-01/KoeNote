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

    bool anyRendered = false;
    bool anyFailed = false;

    for (const auto& region : getPlaybackRegions())
    {
        // 再生位置（プロジェクト時間）上のリージョン範囲と、このブロックの交差部分
        const auto regionRange = region->getSampleRange(sampleRate, juce::ARAPlaybackRegion::IncludeHeadAndTail::no);
        const auto renderRange = blockRange.getIntersectionWith(regionRange);

        if (renderRange.isEmpty())
            continue;

        auto* modification = static_cast<KoeNoteAudioModification*>(region->getAudioModification());
        const auto views = modification->viewsForPlayback();

        // 読み込み未完了のリージョンは鳴らさない（読み込み完了後に鳴る）
        if (! views.edited.valid)
        {
            anyFailed = true;
            continue;
        }

        anyRendered = true;

        const auto startInBlock = static_cast<int>(renderRange.getStart() - blockStart);
        const auto count = static_cast<int>(renderRange.getLength());
        const int outChannels = std::min(buffer.getNumChannels(), numChannels);

        // このブロック先頭の、AudioModification 内の時刻（秒）
        const double modificationStartSec = region->getStartInAudioModificationTime()
                                            + static_cast<double>(renderRange.getStart() - regionRange.getStart()) / sampleRate;

        // [from, to) の出力サンプルを、audio から描画する。
        const auto renderSegment = [&] (const KoeNoteAudioModification::PlaybackAudio& audio, int from, int to)
        {
            if (to <= from)
                return;

            if (std::abs(audio.sampleRate - sampleRate) <= 0.5)
            {
                // リージョン内の位置 → AudioModification 内の位置への変換。リージョンのタイムストレッチは未対応。
                const auto offsetToSource = region->getStartInAudioModificationSamples() - regionRange.getStart();

                render::accumulatePassthrough(audio.view,
                                              renderRange.getStart() + offsetToSource + from,
                                              buffer.getArrayOfWritePointers(),
                                              outChannels,
                                              startInBlock + from,
                                              to - from);
            }
            else
            {
                // 元音声とプロジェクトのサンプルレートが異なる場合は、線形補間で変換する（簡易的なフォールバック）
                const double step = audio.sampleRate / sampleRate;
                const double startPosition = (modificationStartSec + static_cast<double>(from) / sampleRate) * audio.sampleRate;

                render::accumulateResampled(audio.view,
                                            startPosition,
                                            step,
                                            buffer.getArrayOfWritePointers(),
                                            outChannels,
                                            startInBlock + from,
                                            to - from);
            }
        };

        const auto& compare = views.compare;

        if (! compare.useOriginal || ! views.original.valid)
        {
            renderSegment(views.edited, 0, count);
        }
        else if (! compare.rangeOnly)
        {
            // バイパス: 全体を元の音声にする
            renderSegment(views.original, 0, count);
        }
        else
        {
            // 範囲だけ元の音声にする（A/B 比較）。範囲の前後は編集後の音声。
            const auto indexOf = [&] (double sec)
            {
                return static_cast<int>(std::clamp(std::ceil((sec - modificationStartSec) * sampleRate), 0.0, static_cast<double>(count)));
            };

            const int rangeBegin = indexOf(compare.startSec);
            const int rangeEnd = std::max(rangeBegin, indexOf(compare.endSec));

            renderSegment(views.edited, 0, rangeBegin);
            renderSegment(views.original, rangeBegin, rangeEnd);
            renderSegment(views.edited, rangeEnd, count);
        }
    }

    // 一部のリージョンが未読み込みでも、鳴らせたリージョンの音は捨てない。全てが失敗したときだけ false を返す。
    return anyRendered || ! anyFailed;
}
} // namespace koenote::plugin
