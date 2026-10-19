#pragma once

#include "model/EditModel.h"
#include "plugin/EditTarget.h"
#include "plugin/KoeNoteAudioSource.h"
#include "render/PassthroughRenderer.h"
#include "render/ProcessingGate.h"
#include "render/PsolaRenderer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

namespace koenote::plugin
{
// レンダリング済みの音声（編集内容を反映したもの）。出力時間軸（AudioModification の時間）で保持する。
struct RenderedClip
{
    render::RenderedAudio audio;
    std::vector<const float*> channelPointers;
    std::uint64_t revision = 0;

    render::AudioView view() const noexcept
    {
        return { channelPointers.data(), audio.numChannels, audio.numFrames() };
    }
};

// ARAAudioModification の拡張。編集モデルの保持と、解析・レンダリングを担当する。
//
// スレッド:
//  - message thread: getModel() の操作、notifyEdited()、ChangeBroadcaster の通知
//  - worker thread : 解析（F0 推定・ノート分割）とレンダリング。モデルには触れず、値コピーを使う
//  - audio thread  : audioForPlayback() だけ（ProcessingGate の ReadScope 内）
class KoeNoteAudioModification final : public juce::ARAAudioModification,
                                       public EditTarget,
                                       private juce::AsyncUpdater
{
public:
    KoeNoteAudioModification(juce::ARAAudioSource* source,
                             ARA::ARAAudioModificationHostRef hostRef,
                             const juce::ARAAudioModification* modificationToClone,
                             render::ProcessingGate* gate);
    ~KoeNoteAudioModification() override;

    //==========================================================================
    // message thread
    model::EditModel& getModel() noexcept override { return model; }
    const model::EditModel& getModel() const noexcept override { return model; }

    // モデルを編集した後に呼ぶ。レンダリングを要求し、UI とホストへ通知する。
    void notifyEdited() override;
    // 保存データから復元したモデルに差し替える
    void replaceModel(model::EditModel restored);
    // 元音声の読み込み完了時に呼ばれる
    void onSourceAudioReady();

    AnalysisState getAnalysisState() const noexcept override { return analysisState.load(); }
    float getAnalysisProgress() const noexcept override { return analysisProgress.load(); }
    bool isRenderPending() const noexcept override { return renderPending.load(); }

    // 元音声（編集前）。UI の波形表示用。未読み込みなら nullptr。
    std::shared_ptr<const SourceAudioData> getSourceAudio() const override;

    //==========================================================================
    // audio thread（ProcessingGate の ReadScope 内でのみ呼ぶ）
    struct PlaybackAudio
    {
        render::AudioView view;
        double sampleRate = 0.0;
        bool valid = false;
    };

    PlaybackAudio audioForPlayback() const noexcept;

private:
    class Worker;

    void handleAsyncUpdate() override;
    void requestAnalysis();
    void requestRender();
    void publishClip(std::shared_ptr<RenderedClip> clip);

    // Worker から呼ばれる
    void runAnalysis();
    void runRender();

    render::ProcessingGate* gate;
    model::EditModel model;

    std::unique_ptr<Worker> worker;

    std::atomic<AnalysisState> analysisState { AnalysisState::WaitingForAudio };
    std::atomic<float> analysisProgress { 0.0f };

    // worker thread → message thread の受け渡し
    std::mutex handoffMutex;
    struct AnalysisResult
    {
        model::PitchTrack track;
        std::vector<model::Note> notes;
    };
    std::optional<AnalysisResult> pendingAnalysisResult;
    std::atomic<bool> analysisFailed { false };
    std::atomic<bool> renderFinished { false };
    std::atomic<bool> startRequested { false };

    // message thread → worker thread の受け渡し
    struct RenderRequest
    {
        std::vector<model::Note> notes;
        model::PitchTrack track;
        std::uint64_t revision = 0;
    };
    std::optional<RenderRequest> pendingRenderRequest;
    std::atomic<std::uint64_t> latestRequestedRevision { 0 };
    std::atomic<bool> renderPending { false };

    // レンダリング結果。published はオーディオスレッドが参照する生ポインタ（所有は clip）
    std::shared_ptr<RenderedClip> clip;
    std::atomic<const RenderedClip*> published { nullptr };
    std::mutex clipMutex;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KoeNoteAudioModification)
};
} // namespace koenote::plugin
