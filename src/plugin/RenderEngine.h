#pragma once

#include "model/EditModel.h"
#include "plugin/EditTarget.h"
#include "plugin/ISourceAudio.h"
#include "render/PassthroughRenderer.h"
#include "render/ProcessingGate.h"
#include "render/PsolaRenderer.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace koenote::analysis
{
struct AlignmentResult;
}

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

// 解析・整列・レンダリング（TD-PSOLA とニューラル）を worker thread で行い、結果をオーディオスレッドへ
// 安全に公開する。ARA には依存しない（AudioModification が所有し、ARA 固有の処理は呼び出し側が行う）。
//
// スレッド:
//  - message thread: request*()、takeEvents()、各種 getter
//  - worker thread : 解析・整列・レンダリング。モデルには触れず、値のコピーを使う
//  - audio thread  : viewsForPlayback() だけ（ProcessingGate の ReadScope 内）
class RenderEngine
{
public:
    // wake: 結果が用意できたとき（任意のスレッドから）呼ばれる。message thread で takeEvents() を呼ぶきっかけにする。
    RenderEngine(render::ProcessingGate& gate, ISourceAudio& source, std::function<void()> wake);
    ~RenderEngine();

    // worker thread を止める（以降は要求を処理しない）。デストラクタからも呼ばれる。
    void stop();

    RenderEngine(const RenderEngine&) = delete;
    RenderEngine& operator=(const RenderEngine&) = delete;

    //==========================================================================
    // message thread
    struct AnalysisResult
    {
        model::PitchTrack track;
        std::vector<model::Note> notes;
    };

    struct RenderRequest
    {
        std::vector<model::Note> notes;
        model::PitchTrack track;
        model::PitchDrawing drawing; // 手描きのピッチ曲線（F-22）
        std::uint64_t revision = 0;
        model::ModelSettings settings;
    };

    struct Events
    {
        std::optional<AnalysisResult> analysis;
        bool analysisFailed = false;
        std::unique_ptr<analysis::AlignmentResult> alignment;
        std::shared_ptr<const GuideAnalysis> guide; // ガイドの解析結果
        bool renderFinished = false; // 編集済みの音声（またはニューラルの結果）が更新された
    };

    void requestAnalysis();
    // ガイドの音声を、このクリップと同じ方法（F0 推定・ノート分割）で解析する。結果は Events::guide。
    void requestGuideAnalysis(std::shared_ptr<const SourceAudioData> audio);
    // 最新の要求だけが処理される。実行中のレンダリングは、新しい要求が来ると中断される。
    void requestRender(RenderRequest request);
    // startOffsetSec: ガイド上の時刻 = ターゲットの時刻 + startOffsetSec（整列の開始位置のずれ）
    void requestAlign(std::shared_ptr<const SourceAudioData> guide, double startOffsetSec = 0.0);
    // 実行中・待機中のレンダリングを取り消し、編集済みの音声を捨てて元音声へ戻す
    void resetRendering();

    Events takeEvents();

    float analysisProgress() const noexcept { return analysisProgressValue.load(); }
    bool isRenderPending() const noexcept { return renderPending.load(); }
    // 書き出し（オフライン処理）に使える状態か。レンダリングの要求が残っておらず、ニューラル加工を使う場合は
    // 最新の要求に対する合成が終わっている（成功・失敗のどちらでも）。atomic だけを読むため、どのスレッドからも呼べる。
    bool isSettled() const noexcept;
    AlignInfo alignInfo() const noexcept { return { alignState.load(), alignProgress.load() }; }
    // state == Unavailable のときの理由・状態は、呼び出し側が NeuralService から補う
    NeuralInfo neuralInfo() const;
    void markAlignFailed() { alignState = AlignInfo::State::Failed; }
    void markAlignDone() { alignState = AlignInfo::State::Done; }

    void setCompare(const CompareState& state);
    CompareState getCompare() const;

    // ニューラル加工を始めるまでの待ち時間（編集が続いている間は始めない）。テストでは短くする。
    void setNeuralDebounceMs(int ms) noexcept { neuralDebounceMs = ms; }

    //==========================================================================
    // audio thread（ProcessingGate の ReadScope 内でのみ呼ぶ）
    struct PlaybackAudio
    {
        render::AudioView view;
        double sampleRate = 0.0;
        bool valid = false;
    };

    struct PlaybackViews
    {
        PlaybackAudio edited;   // 編集後（レンダリング済みがなければ元音声）
        PlaybackAudio original; // 元音声
        CompareState compare;
    };

    PlaybackViews viewsForPlayback() const noexcept;

private:
    class Worker;

    struct PendingRender
    {
        RenderRequest request;
        std::uint64_t token = 0;
    };

    void runAnalysis();
    void runAlign();
    void runGuideAnalysis();
    void runRender();
    void runNeural(const std::shared_ptr<const SourceAudioData>& audio,
                   const std::vector<model::Note>& notes,
                   const model::PitchTrack& track,
                   const model::PitchDrawing& drawing,
                   const render::TimeMap::Pins& pins,
                   std::uint64_t token,
                   std::uint64_t revision);
    void publishClip(std::shared_ptr<RenderedClip> clip, std::uint64_t revision);
    void publishNeural(std::shared_ptr<RenderedClip> clip);
    void ensureWorker();

    render::ProcessingGate& gate;
    ISourceAudio& source;
    std::function<void()> wake;

    std::unique_ptr<Worker> worker;

    std::mutex handoffMutex;
    std::optional<AnalysisResult> pendingAnalysisResult;
    std::unique_ptr<analysis::AlignmentResult> pendingAlignment;
    std::optional<PendingRender> pendingRender;
    std::shared_ptr<const SourceAudioData> pendingGuide;
    double pendingGuideOffset = 0.0;
    std::shared_ptr<const SourceAudioData> pendingGuideAnalysisAudio;
    std::shared_ptr<const GuideAnalysis> pendingGuideResult;
    bool analysisFailedFlag = false;
    bool renderFinishedFlag = false;

    std::atomic<std::uint64_t> latestRequestToken { 0 };
    std::atomic<float> analysisProgressValue { 0.0f };
    std::atomic<bool> renderPending { false };
    // ニューラル加工の対象の要求（トークン。0 は対象なし）と、合成が終わった要求
    std::atomic<std::uint64_t> neuralTargetToken { 0 };
    std::atomic<std::uint64_t> neuralDoneToken { 0 };

    // レンダリング結果。published はオーディオスレッドが参照する生ポインタ（所有は clip）
    std::shared_ptr<RenderedClip> clip;
    std::atomic<const RenderedClip*> published { nullptr };
    std::atomic<std::uint64_t> publishedRevision { 0 };
    std::shared_ptr<RenderedClip> neuralClip;
    std::atomic<const RenderedClip*> neuralPublished { nullptr };
    std::mutex clipMutex;

    // A/B 比較（audio thread が読む）
    std::atomic<bool> compareUseOriginal { false };
    std::atomic<bool> compareRangeOnly { false };
    std::atomic<double> compareStart { 0.0 };
    std::atomic<double> compareEnd { 0.0 };

    std::atomic<NeuralInfo::State> neuralState { NeuralInfo::State::Unavailable };
    std::atomic<float> neuralProgress { 0.0f };
    std::atomic<AlignInfo::State> alignState { AlignInfo::State::Idle };
    std::atomic<float> alignProgress { 0.0f };
    mutable std::mutex messageMutex;
    juce::String neuralMessage;
    std::atomic<int> neuralDebounceMs { 500 };
};
} // namespace koenote::plugin
