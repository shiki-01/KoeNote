#include "plugin/KoeNoteAudioModification.h"

#include "analysis/RuleBasedNoteSegmenter.h"
#include "analysis/YinPitchAnalyzer.h"

#include <algorithm>

namespace koenote::plugin
{
// 解析とレンダリングを行う専用スレッド。要求は「最新のものだけ」処理する。
class KoeNoteAudioModification::Worker final : public juce::Thread
{
public:
    explicit Worker(KoeNoteAudioModification& ownerIn) : juce::Thread("KoeNote worker"), owner(ownerIn) {}
    ~Worker() override { stopThread(15000); }

    void requestAnalysis()
    {
        analysisRequested = true;
        notify();
    }

    void requestRender()
    {
        renderRequested = true;
        notify();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            bool didWork = false;

            if (analysisRequested.exchange(false))
            {
                owner.runAnalysis();
                didWork = true;
            }

            if (threadShouldExit())
                break;

            if (renderRequested.exchange(false))
            {
                owner.runRender();
                didWork = true;
            }

            if (! didWork)
                wait(-1);
        }
    }

private:
    KoeNoteAudioModification& owner;
    std::atomic<bool> analysisRequested { false };
    std::atomic<bool> renderRequested { false };
};

//==============================================================================
KoeNoteAudioModification::KoeNoteAudioModification(juce::ARAAudioSource* source,
                                                   ARA::ARAAudioModificationHostRef hostRef,
                                                   const juce::ARAAudioModification* modificationToClone,
                                                   render::ProcessingGate* gateIn)
    : juce::ARAAudioModification(source, hostRef, modificationToClone), gate(gateIn)
{
    if (modificationToClone != nullptr)
    {
        const auto* other = static_cast<const KoeNoteAudioModification*>(modificationToClone);
        model = other->model;

        if (model.hasAnalysis())
            analysisState = AnalysisState::Done;
    }

    // 元音声が既に読み込み済みの場合に備え、構築の完了後に message thread で開始する
    startRequested = true;
    triggerAsyncUpdate();
}

KoeNoteAudioModification::~KoeNoteAudioModification()
{
    notifyWillBeDestroyed();
    cancelPendingUpdate();
    worker.reset(); // スレッドの終了を待つ
}

std::shared_ptr<const SourceAudioData> KoeNoteAudioModification::getSourceAudio() const
{
    return static_cast<const KoeNoteAudioSource*>(getAudioSource())->sharedAudio();
}

KoeNoteAudioModification::PlaybackAudio KoeNoteAudioModification::audioForPlayback() const noexcept
{
    if (const auto* rendered = published.load(std::memory_order_acquire))
        return { rendered->view(), rendered->audio.sampleRate, true };

    const auto* source = static_cast<const KoeNoteAudioSource*>(getAudioSource());

    if (const auto* data = source->audioForPlayback())
        return { data->view(), data->sampleRate, true };

    return {};
}

//==============================================================================
void KoeNoteAudioModification::onSourceAudioReady()
{
    if (! model.hasAnalysis())
    {
        analysisState = AnalysisState::Analyzing;
        analysisProgress = 0.0f;
        requestAnalysis();
    }
    else
    {
        analysisState = AnalysisState::Done;
        requestRender();
    }

    sendChangeMessage();
}

void KoeNoteAudioModification::replaceModel(model::EditModel restored)
{
    model = std::move(restored);
    analysisState = model.hasAnalysis() ? AnalysisState::Done : AnalysisState::WaitingForAudio;

    // 音声が読み込み済みなら、復元した編集内容を反映する
    if (getAudioSource<KoeNoteAudioSource>()->isLoaded())
        onSourceAudioReady();
    else
        sendChangeMessage();
}

void KoeNoteAudioModification::requestReanalysis()
{
    // 編集内容は破棄される。再生は解析が終わるまで元音声のままになる。
    model = model::EditModel {};
    analysisState = AnalysisState::WaitingForAudio;

    // 実行中・待機中のレンダリングを取り消し、編集済みの音声を捨てて元音声へ戻す
    ++latestRequestToken;
    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        pendingRenderRequest.reset();
    }
    renderPending = false;
    publishClip(nullptr);
    renderFinished = true;
    triggerAsyncUpdate();

    if (getAudioSource<KoeNoteAudioSource>()->isLoaded())
        onSourceAudioReady();

    sendChangeMessage();
}

void KoeNoteAudioModification::notifyEdited()
{
    requestRender();
    sendChangeMessage();
}

void KoeNoteAudioModification::requestAnalysis()
{
    if (worker == nullptr)
    {
        worker = std::make_unique<Worker>(*this);
        worker->startThread();
    }

    worker->requestAnalysis();
}

void KoeNoteAudioModification::requestRender()
{
    if (! model.hasAnalysis())
        return;

    RenderRequest request;
    request.notes = model.notes();
    request.track = model.pitchTrack();
    request.revision = model.revision();
    request.token = ++latestRequestToken;

    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        pendingRenderRequest = std::move(request);
    }

    renderPending = true;

    if (worker == nullptr)
    {
        worker = std::make_unique<Worker>(*this);
        worker->startThread();
    }

    worker->requestRender();
}

//==============================================================================
// worker thread
void KoeNoteAudioModification::runAnalysis()
{
    const auto audio = getSourceAudio();

    if (audio == nullptr || audio->numFrames <= 0 || audio->numChannels <= 0)
        return;

    // 解析はモノラル化した音声で行う
    std::vector<float> mono(static_cast<std::size_t>(audio->numFrames), 0.0f);
    const float gain = 1.0f / static_cast<float>(audio->numChannels);

    for (const auto& channel : audio->channels)
        for (std::size_t i = 0; i < mono.size(); ++i)
            mono[i] += channel[i] * gain;

    const analysis::MonoAudioView view { mono.data(), mono.size(), audio->sampleRate };

    analysis::YinPitchAnalyzer analyzer;
    auto track = analyzer.analyze(view, [this] (float p)
    {
        analysisProgress = p;
        return ! juce::Thread::currentThreadShouldExit();
    });

    if (juce::Thread::currentThreadShouldExit())
        return;

    if (track.empty())
    {
        analysisFailed = true;
        triggerAsyncUpdate();
        return;
    }

    analysis::RuleBasedNoteSegmenter segmenter;
    auto notes = segmenter.segment(track, view);

    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        pendingAnalysisResult = AnalysisResult { std::move(track), std::move(notes) };
    }

    triggerAsyncUpdate();
}

// worker thread
void KoeNoteAudioModification::runRender()
{
    std::optional<RenderRequest> request;

    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        request = std::move(pendingRenderRequest);
        pendingRenderRequest.reset();
    }

    if (! request.has_value())
        return;

    const auto audio = getSourceAudio();

    if (audio == nullptr)
        return;

    std::shared_ptr<RenderedClip> newClip;

    if (render::hasAudibleEdits(request->notes))
    {
        render::RenderInput input;
        input.channels = audio->channelPointers.data();
        input.numChannels = audio->numChannels;
        input.numFrames = audio->numFrames;
        input.sampleRate = audio->sampleRate;
        input.track = &request->track;
        input.notes = &request->notes;

        const auto token = request->token;

        auto rendered = render::renderEditedAudio(input, [this, token] (float)
        {
            // より新しい要求が来ていたら中断する
            return ! juce::Thread::currentThreadShouldExit() && latestRequestToken.load() == token;
        });

        if (rendered.numChannels == 0)
            return; // 中断（新しい要求が処理される）

        newClip = std::make_shared<RenderedClip>();
        newClip->audio = std::move(rendered);
        newClip->revision = request->revision;

        for (const auto& channel : newClip->audio.channels)
            newClip->channelPointers.push_back(channel.data());
    }

    // 実行中に要求が取り消された・更新された場合は、古い結果を公開しない
    if (latestRequestToken.load() != request->token)
        return;

    publishClip(std::move(newClip));
    renderPending = false;

    renderFinished = true;
    triggerAsyncUpdate();
}

void KoeNoteAudioModification::publishClip(std::shared_ptr<RenderedClip> newClip)
{
    std::shared_ptr<RenderedClip> previous;

    {
        render::ProcessingGate::WriteScope writeScope(*gate);
        const std::lock_guard<std::mutex> lock(clipMutex);
        previous = std::move(clip);
        clip = std::move(newClip);
        published.store(clip.get(), std::memory_order_release);
    }

    // previous はゲートの外で解放される
}

//==============================================================================
// message thread
void KoeNoteAudioModification::handleAsyncUpdate()
{
    if (startRequested.exchange(false))
    {
        if (getAudioSource<KoeNoteAudioSource>()->isLoaded())
            onSourceAudioReady();
    }

    std::optional<AnalysisResult> result;

    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        result = std::move(pendingAnalysisResult);
        pendingAnalysisResult.reset();
    }

    if (result.has_value())
    {
        // 解析中にモデルが復元された場合は、そちらを優先する
        if (! model.hasAnalysis())
            model.setAnalysis(std::move(result->track), std::move(result->notes));

        analysisState = AnalysisState::Done;
        analysisProgress = 1.0f;
        sendChangeMessage();
    }

    if (analysisFailed.exchange(false))
    {
        analysisState = AnalysisState::Failed;
        sendChangeMessage();
    }

    if (renderFinished.exchange(false))
    {
        // 再生系（ホスト）へ、サンプルが変わったことを知らせる
        notifyContentChanged(juce::ARAContentUpdateScopes::samplesAreAffected(), true);

        for (auto* region : getPlaybackRegions())
            region->notifyContentChanged(juce::ARAContentUpdateScopes::samplesAreAffected(), true);

        sendChangeMessage();
    }
}
} // namespace koenote::plugin
