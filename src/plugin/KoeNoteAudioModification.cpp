#include "plugin/KoeNoteAudioModification.h"

#include "analysis/DtwAligner.h"
#include "analysis/RuleBasedNoteSegmenter.h"
#include "analysis/YinPitchAnalyzer.h"
#include "neural/NeuralRenderer.h"
#include "plugin/NeuralService.h"

#include <algorithm>

namespace koenote::plugin
{
namespace
{
// 複数チャンネルをモノラル化する
std::vector<float> toMono(const SourceAudioData& audio)
{
    std::vector<float> mono(static_cast<std::size_t>(audio.numFrames), 0.0f);
    const float gain = 1.0f / static_cast<float>(audio.numChannels);

    for (const auto& channel : audio.channels)
        for (std::size_t i = 0; i < mono.size(); ++i)
            mono[i] += channel[i] * gain;

    return mono;
}
} // namespace

// 解析・整列・レンダリングを行う専用スレッド。要求は「最新のものだけ」処理する。
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

    void requestAlign()
    {
        alignRequested = true;
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

            if (alignRequested.exchange(false))
            {
                owner.runAlign();
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
    std::atomic<bool> alignRequested { false };
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

KoeNoteAudioModification::PlaybackViews KoeNoteAudioModification::viewsForPlayback() const noexcept
{
    PlaybackViews views;

    const auto* source = static_cast<const KoeNoteAudioSource*>(getAudioSource());
    if (const auto* data = source->audioForPlayback())
        views.original = { data->view(), data->sampleRate, true };

    const auto* rendered = published.load(std::memory_order_acquire);
    const auto* neural = neuralPublished.load(std::memory_order_acquire);

    // ニューラルの結果は、最新の編集内容に対するものだけを使う（古ければ TD-PSOLA の結果で補う）
    if (neural != nullptr && neural->revision == publishedRevision.load(std::memory_order_acquire))
        views.edited = { neural->view(), neural->audio.sampleRate, true };
    else if (rendered != nullptr)
        views.edited = { rendered->view(), rendered->audio.sampleRate, true };
    else
        views.edited = views.original;

    views.compare.useOriginal = compareUseOriginal.load(std::memory_order_relaxed);
    views.compare.rangeOnly = compareRangeOnly.load(std::memory_order_relaxed);
    views.compare.startSec = compareStart.load(std::memory_order_relaxed);
    views.compare.endSec = compareEnd.load(std::memory_order_relaxed);
    return views;
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
    publishNeural(nullptr);
    publishClip(nullptr, 0);
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
    request.settings = model.settings();

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
// テンポ・時間のずれ（F-14）
static juce::int64 nowMs() { return static_cast<juce::int64>(juce::Time::getMillisecondCounter()); }

model::TempoMap KoeNoteAudioModification::readHostTempo() const
{
    const auto& regions = getPlaybackRegions();
    if (regions.empty())
        return {};

    auto* sequence = regions.front()->getRegionSequence<juce::ARARegionSequence>();
    auto* context = sequence != nullptr ? sequence->getMusicalContext<juce::ARAMusicalContext>() : nullptr;
    if (context == nullptr)
        return {};

    // ホストのテンポ情報（kARAContentTypeTempoEntries）。ARA のメインスレッドで読む。
    ARA::PlugIn::HostContentReader<ARA::kARAContentTypeTempoEntries> reader(context);
    if (! reader)
        return {};

    std::vector<model::TempoMap::Entry> entries;
    entries.reserve(static_cast<std::size_t>(reader.getEventCount()));

    for (ARA::ARAInt32 i = 0; i < reader.getEventCount(); ++i)
    {
        const auto e = reader[i];
        entries.push_back({ e.timePosition, e.quarterPosition });
    }

    return model::TempoMap(std::move(entries));
}

model::TempoMap KoeNoteAudioModification::getTempoMap() const
{
    const auto now = nowMs();

    if (now - hostTempoReadAt > 1000 || hostTempoReadAt == 0)
    {
        hostTempo = readHostTempo();
        hostTempoReadAt = now;
    }

    if (hostTempo.valid())
        return hostTempo;

    // ホストがテンポを提供しない場合は、手動の BPM を使う
    return model::TempoMap::constant(static_cast<double>(model.settings().manualBpm));
}

double KoeNoteAudioModification::getTimeOffsetSec() const
{
    const auto& regions = getPlaybackRegions();
    if (regions.empty())
        return 0.0;

    return regions.front()->getStartInPlaybackTime() - regions.front()->getStartInAudioModificationTime();
}

//==============================================================================
// A/B 比較（F-18）
void KoeNoteAudioModification::setCompare(const CompareState& state)
{
    compareStart = state.startSec;
    compareEnd = state.endSec;
    compareRangeOnly = state.rangeOnly;
    compareUseOriginal = state.useOriginal;
    sendChangeMessage();
}

CompareState KoeNoteAudioModification::getCompare() const
{
    CompareState s;
    s.useOriginal = compareUseOriginal.load();
    s.rangeOnly = compareRangeOnly.load();
    s.startSec = compareStart.load();
    s.endSec = compareEnd.load();
    return s;
}

//==============================================================================
// タイミング整列（F-13）
std::vector<GuideCandidate> KoeNoteAudioModification::getGuideCandidates() const
{
    std::vector<GuideCandidate> candidates;
    const auto* self = getAudioSource();
    int index = 0;

    for (auto* source : self->getDocument()->getAudioSources<KoeNoteAudioSource>())
    {
        const int id = index++;

        if (source == self || ! source->isLoaded())
            continue;

        GuideCandidate c;
        c.id = id;
        const char* name = source->getName();
        c.name = (name != nullptr && *name != 0) ? juce::String::fromUTF8(name) : "Audio " + juce::String(id + 1);
        c.durationSec = source->getDuration();
        candidates.push_back(c);
    }

    return candidates;
}

void KoeNoteAudioModification::alignToGuide(int guideId)
{
    int index = 0;

    for (auto* source : getAudioSource()->getDocument()->getAudioSources<KoeNoteAudioSource>())
    {
        if (index++ == guideId)
        {
            alignToGuideAudio(source->sharedAudio());
            return;
        }
    }
}

void KoeNoteAudioModification::alignToGuideAudio(std::shared_ptr<const SourceAudioData> guide)
{
    if (guide == nullptr || ! model.hasAnalysis() || getSourceAudio() == nullptr)
    {
        alignState = AlignInfo::State::Failed;
        sendChangeMessage();
        return;
    }

    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        pendingGuide = std::move(guide);
    }

    alignState = AlignInfo::State::Working;
    alignProgress = 0.0f;

    if (worker == nullptr)
    {
        worker = std::make_unique<Worker>(*this);
        worker->startThread();
    }

    worker->requestAlign();
    sendChangeMessage();
}

AlignInfo KoeNoteAudioModification::getAlignInfo() const
{
    return { alignState.load(), alignProgress.load() };
}

void KoeNoteAudioModification::applyAlignment(const analysis::AlignmentResult& result)
{
    if (! result.valid || ! model.hasAnalysis())
    {
        alignState = AlignInfo::State::Failed;
        return;
    }

    // 各ノートの区間を、ガイド上の対応する時刻へ移す（出力時間軸 = ガイドの時間）
    std::vector<std::pair<model::NoteId, model::NoteEdit>> edits;

    for (const auto& note : model.notes())
    {
        auto edit = note.edit;
        const double start = std::max(0.0, result.mapTargetToGuide(note.startSec));
        const double end = std::max(start + model::EditModel::kMinNoteDurationSec, result.mapTargetToGuide(note.endSec));
        edit.targetStartSec = start;
        edit.targetEndSec = end;
        edits.emplace_back(note.id, edit);
    }

    if (model.applyEdits(edits))
    {
        alignState = AlignInfo::State::Done;
        notifyEdited();
    }
    else
    {
        alignState = AlignInfo::State::Failed;
    }
}

//==============================================================================
NeuralInfo KoeNoteAudioModification::getNeuralInfo() const
{
    NeuralInfo info;
    info.state = neuralState.load();
    info.progress = neuralProgress.load();

    {
        const std::lock_guard<std::mutex> lock(messageMutex);
        info.message = neuralMessage;
    }

    // 利用できない場合は、理由を探索結果から得る
    if (info.state == NeuralInfo::State::Unavailable)
    {
        const auto package = NeuralService::get().package();
        info.message = package.available ? juce::String("") : juce::String(package.reason);

        if (package.available)
            info.state = NeuralInfo::State::Idle;
    }

    return info;
}

//==============================================================================
// worker thread
void KoeNoteAudioModification::runAnalysis()
{
    const auto audio = getSourceAudio();

    if (audio == nullptr || audio->numFrames <= 0 || audio->numChannels <= 0)
        return;

    // 解析はモノラル化した音声で行う
    const auto mono = toMono(*audio);
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
void KoeNoteAudioModification::runAlign()
{
    std::shared_ptr<const SourceAudioData> guide;

    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        guide = std::move(pendingGuide);
        pendingGuide.reset();
    }

    const auto target = getSourceAudio();

    if (guide == nullptr || target == nullptr)
        return;

    const auto guideMono = toMono(*guide);
    const auto targetMono = toMono(*target);
    const analysis::MonoAudioView guideView { guideMono.data(), guideMono.size(), guide->sampleRate };
    const analysis::MonoAudioView targetView { targetMono.data(), targetMono.size(), target->sampleRate };

    auto result = analysis::alignAudio(guideView, targetView, {}, [this] (float p)
    {
        alignProgress = p;
        return ! juce::Thread::currentThreadShouldExit();
    });

    if (juce::Thread::currentThreadShouldExit())
        return;

    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        pendingAlignment = std::make_unique<analysis::AlignmentResult>(std::move(result));
    }

    alignFinished = true;
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
    const bool audible = render::hasAudibleEdits(request->notes);

    if (audible)
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

    publishClip(std::move(newClip), request->revision);
    renderPending = false;

    renderFinished = true;
    triggerAsyncUpdate();

    // 高品質のニューラル加工（F-17）。先に TD-PSOLA の結果が再生されるため、音切れは出ない。
    if (request->settings.renderMode == 1 && audible)
    {
        runNeural(audio, request->notes, request->track, request->token, request->revision);
    }
    else
    {
        publishNeural(nullptr);
        neuralState = NeuralInfo::State::Unavailable; // getNeuralInfo が探索結果から状態を決める
    }
}

// worker thread
void KoeNoteAudioModification::runNeural(const std::shared_ptr<const SourceAudioData>& audio,
                                         const std::vector<model::Note>& notes,
                                         const model::PitchTrack& track,
                                         std::uint64_t token,
                                         std::uint64_t revision)
{
    const auto cancelled = [&] { return juce::Thread::currentThreadShouldExit() || latestRequestToken.load() != token; };

    // 編集が続いている間は始めない（約 0.5 秒、新しい要求がなければ開始する）
    for (int i = 0; i < 10; ++i)
    {
        juce::Thread::sleep(50);

        if (cancelled())
            return;
    }

    neuralProgress = 0.0f;
    neuralState = NeuralInfo::State::Rendering;
    triggerAsyncUpdate();

    std::string error;
    auto vocoder = NeuralService::get().vocoder(error);

    if (vocoder == nullptr)
    {
        {
            const std::lock_guard<std::mutex> lock(messageMutex);
            neuralMessage = juce::String::fromUTF8(error.c_str());
        }
        neuralState = NeuralInfo::State::Failed;
        triggerAsyncUpdate();
        return;
    }

    render::RenderInput input;
    input.channels = audio->channelPointers.data();
    input.numChannels = audio->numChannels;
    input.numFrames = audio->numFrames;
    input.sampleRate = audio->sampleRate;
    input.track = &track;
    input.notes = &notes;

    auto rendered = neural::renderNeural(input, *vocoder, [&] (float p)
    {
        neuralProgress = p;
        return ! cancelled();
    }, error);

    if (rendered.numChannels == 0)
    {
        if (! error.empty())
        {
            {
                const std::lock_guard<std::mutex> lock(messageMutex);
                neuralMessage = juce::String::fromUTF8(error.c_str());
            }
            neuralState = NeuralInfo::State::Failed;
        }
        else
        {
            neuralState = NeuralInfo::State::Idle; // 中断（新しい要求が処理される）
        }

        triggerAsyncUpdate();
        return;
    }

    if (cancelled())
        return;

    auto result = std::make_shared<RenderedClip>();
    result->audio = std::move(rendered);
    result->revision = revision;

    for (const auto& channel : result->audio.channels)
        result->channelPointers.push_back(channel.data());

    publishNeural(std::move(result));
    neuralState = NeuralInfo::State::Ready;
    neuralProgress = 1.0f;
    renderFinished = true;
    triggerAsyncUpdate();
}

void KoeNoteAudioModification::publishClip(std::shared_ptr<RenderedClip> newClip, std::uint64_t revision)
{
    std::shared_ptr<RenderedClip> previous;

    {
        render::ProcessingGate::WriteScope writeScope(*gate);
        const std::lock_guard<std::mutex> lock(clipMutex);
        previous = std::move(clip);
        clip = std::move(newClip);
        published.store(clip.get(), std::memory_order_release);
        publishedRevision.store(revision, std::memory_order_release);
    }

    // previous はゲートの外で解放される
}

void KoeNoteAudioModification::publishNeural(std::shared_ptr<RenderedClip> newClip)
{
    std::shared_ptr<RenderedClip> previous;

    {
        render::ProcessingGate::WriteScope writeScope(*gate);
        const std::lock_guard<std::mutex> lock(clipMutex);
        previous = std::move(neuralClip);
        neuralClip = std::move(newClip);
        neuralPublished.store(neuralClip.get(), std::memory_order_release);
    }
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
    std::unique_ptr<analysis::AlignmentResult> alignment;

    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        result = std::move(pendingAnalysisResult);
        pendingAnalysisResult.reset();
        alignment = std::move(pendingAlignment);
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

    if (alignFinished.exchange(false) && alignment != nullptr)
    {
        applyAlignment(*alignment);
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

    // ニューラルの進捗表示の更新
    if (neuralState.load() == NeuralInfo::State::Rendering || neuralState.load() == NeuralInfo::State::Failed)
        sendChangeMessage();
}
} // namespace koenote::plugin
