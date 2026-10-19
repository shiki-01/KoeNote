#pragma once

#include "render/ProcessingGate.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace koenote::plugin
{
// ARA の再生レンダラー。フェーズ0 では元音声をそのまま返す（パススルー）。
// processBlock はオーディオスレッドで呼ばれるため、メモリ確保・ロック・ファイル I/O・ログ出力を行わない。
class KoeNotePlaybackRenderer final : public juce::ARAPlaybackRenderer
{
public:
    KoeNotePlaybackRenderer(ARA::PlugIn::DocumentController* controller, render::ProcessingGate& gateIn)
        : juce::ARAPlaybackRenderer(controller), gate(gateIn) {}

    void prepareToPlay(double sampleRate,
                       int maximumSamplesPerBlock,
                       int numChannels,
                       juce::AudioProcessor::ProcessingPrecision precision,
                       AlwaysNonRealtime alwaysNonRealtime) override;
    void releaseResources() override;

    bool processBlock(juce::AudioBuffer<float>& buffer,
                      juce::AudioProcessor::Realtime realtime,
                      const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept override;

    using juce::ARAPlaybackRenderer::processBlock;

private:
    // オフライン処理（書き出し）のとき、全リージョンの解析・レンダリング・ニューラル加工が終わるまで待つ。
    // リアルタイム再生では呼ばない。待ち時間の上限を超えたら、その時点の結果（TD-PSOLA など）で処理を続ける。
    void waitUntilReadyForExport() noexcept;

    render::ProcessingGate& gate;
    double sampleRate = 48000.0;
    int numChannels = 2;

    // 書き出しの待ち時間の累計（ミリ秒）。全リージョンの準備ができたら 0 に戻す。
    int exportWaitMs = 0;
};
} // namespace koenote::plugin
