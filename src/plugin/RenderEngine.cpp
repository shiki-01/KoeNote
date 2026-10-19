#include "plugin/RenderEngine.h"

#include "analysis/DtwAligner.h"
#include "analysis/RuleBasedNoteSegmenter.h"
#include "analysis/YinPitchAnalyzer.h"
#include "neural/NeuralRenderer.h"
#include "plugin/NeuralService.h"

#include <algorithm>
#include <cmath>

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
class RenderEngine::Worker final : public juce::Thread
{
public:
    explicit Worker(RenderEngine& ownerIn) : juce::Thread("KoeNote worker"), owner(ownerIn) {}
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
    RenderEngine& owner;
    std::atomic<bool> analysisRequested { false };
    std::atomic<bool> alignRequested { false };
    std::atomic<bool> renderRequested { false };
};

//==============================================================================
RenderEngine::RenderEngine(render::ProcessingGate& gateIn, ISourceAudio& sourceIn, std::function<void()> wakeIn)
    : gate(gateIn), source(sourceIn), wake(std::move(wakeIn))
{
}

RenderEngine::~RenderEngine()
{
    stop();
}

void RenderEngine::stop()
{
    worker.reset(); // スレッドの終了を待つ
}

void RenderEngine::ensureWorker()
{
    if (worker == nullptr)
    {
        worker = std::make_unique<Worker>(*this);
        worker->startThread();
    }
}

void RenderEngine::requestAnalysis()
{
    analysisProgressValue = 0.0f;
    ensureWorker();
    worker->requestAnalysis();
}

void RenderEngine::requestRender(RenderRequest request)
{
    PendingRender pending;
    pending.request = std::move(request);
    pending.token = ++latestRequestToken;

    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        pendingRender = std::move(pending);
    }

    renderPending = true;
    ensureWorker();
    worker->requestRender();
}

void RenderEngine::requestAlign(std::shared_ptr<const SourceAudioData> guide, double startOffsetSec)
{
    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        pendingGuide = std::move(guide);
        pendingGuideOffset = std::isfinite(startOffsetSec) ? startOffsetSec : 0.0;
    }

    alignState = AlignInfo::State::Working;
    alignProgress = 0.0f;
    ensureWorker();
    worker->requestAlign();
}

void RenderEngine::resetRendering()
{
    ++latestRequestToken;

    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        pendingRender.reset();
        renderFinishedFlag = true;
    }

    renderPending = false;
    publishNeural(nullptr);
    publishClip(nullptr, 0);

    if (wake)
        wake();
}

RenderEngine::Events RenderEngine::takeEvents()
{
    Events events;
    const std::lock_guard<std::mutex> lock(handoffMutex);

    events.analysis = std::move(pendingAnalysisResult);
    pendingAnalysisResult.reset();
    events.alignment = std::move(pendingAlignment);
    events.analysisFailed = analysisFailedFlag;
    events.renderFinished = renderFinishedFlag;
    analysisFailedFlag = false;
    renderFinishedFlag = false;
    return events;
}

//==============================================================================
void RenderEngine::setCompare(const CompareState& state)
{
    compareStart = state.startSec;
    compareEnd = state.endSec;
    compareRangeOnly = state.rangeOnly;
    compareUseOriginal = state.useOriginal;
}

CompareState RenderEngine::getCompare() const
{
    CompareState s;
    s.useOriginal = compareUseOriginal.load();
    s.rangeOnly = compareRangeOnly.load();
    s.startSec = compareStart.load();
    s.endSec = compareEnd.load();
    return s;
}

NeuralInfo RenderEngine::neuralInfo() const
{
    NeuralInfo info;
    info.state = neuralState.load();
    info.progress = neuralProgress.load();

    const std::lock_guard<std::mutex> lock(messageMutex);
    info.message = neuralMessage;
    return info;
}

RenderEngine::PlaybackViews RenderEngine::viewsForPlayback() const noexcept
{
    PlaybackViews views;

    if (const auto* data = source.audioForPlayback())
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
// worker thread
void RenderEngine::runAnalysis()
{
    const auto audio = source.sharedAudio();

    if (audio == nullptr || audio->numFrames <= 0 || audio->numChannels <= 0)
        return;

    // 解析はモノラル化した音声で行う
    const auto mono = toMono(*audio);
    const analysis::MonoAudioView view { mono.data(), mono.size(), audio->sampleRate };

    analysis::YinPitchAnalyzer analyzer;
    auto track = analyzer.analyze(view, [this] (float p)
    {
        analysisProgressValue = p;
        return ! juce::Thread::currentThreadShouldExit();
    });

    if (juce::Thread::currentThreadShouldExit())
        return;

    if (track.empty())
    {
        {
            const std::lock_guard<std::mutex> lock(handoffMutex);
            analysisFailedFlag = true;
        }

        if (wake)
            wake();

        return;
    }

    analysis::RuleBasedNoteSegmenter segmenter;
    auto notes = segmenter.segment(track, view);

    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        pendingAnalysisResult = AnalysisResult { std::move(track), std::move(notes) };
    }

    analysisProgressValue = 1.0f;

    if (wake)
        wake();
}

// worker thread
void RenderEngine::runAlign()
{
    std::shared_ptr<const SourceAudioData> guide;
    double offset = 0.0;

    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        guide = std::move(pendingGuide);
        pendingGuide.reset();
        offset = pendingGuideOffset;
    }

    const auto target = source.sharedAudio();

    if (guide == nullptr || target == nullptr)
    {
        alignState = AlignInfo::State::Failed;
        return;
    }

    const auto guideMono = toMono(*guide);
    const auto targetMono = toMono(*target);

    // 開始位置のずれ: 「ガイドの時刻 = ターゲットの時刻 + offset」。ずれの分だけ、先に始まっている側の頭を切り落として整列し、
    // 結果の対応表を元の時刻へ戻す。
    const double dropGuide = std::max(0.0, offset);
    const double dropTarget = std::max(0.0, -offset);
    const auto guideSkip = std::min(guideMono.size(), static_cast<std::size_t>(dropGuide * guide->sampleRate));
    const auto targetSkip = std::min(targetMono.size(), static_cast<std::size_t>(dropTarget * target->sampleRate));

    const analysis::MonoAudioView guideView { guideMono.data() + guideSkip, guideMono.size() - guideSkip, guide->sampleRate };
    const analysis::MonoAudioView targetView { targetMono.data() + targetSkip, targetMono.size() - targetSkip, target->sampleRate };

    auto result = analysis::alignAudio(guideView, targetView, {}, [this] (float p)
    {
        alignProgress = p;
        return ! juce::Thread::currentThreadShouldExit();
    });

    if (result.valid)
    {
        for (auto& anchor : result.path)
        {
            anchor.guideSec += dropGuide;
            anchor.targetSec += dropTarget;
        }
    }

    if (juce::Thread::currentThreadShouldExit())
        return;

    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        pendingAlignment = std::make_unique<analysis::AlignmentResult>(std::move(result));
    }

    if (wake)
        wake();
}

// worker thread
void RenderEngine::runRender()
{
    std::optional<PendingRender> pending;

    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        pending = std::move(pendingRender);
        pendingRender.reset();
    }

    if (! pending.has_value())
        return;

    const auto audio = source.sharedAudio();

    if (audio == nullptr)
        return;

    auto& request = pending->request;
    const auto token = pending->token;

    std::shared_ptr<RenderedClip> newClip;
    const bool audible = render::hasAudibleEdits(request.notes);

    if (audible)
    {
        render::RenderInput input;
        input.channels = audio->channelPointers.data();
        input.numChannels = audio->numChannels;
        input.numFrames = audio->numFrames;
        input.sampleRate = audio->sampleRate;
        input.track = &request.track;
        input.notes = &request.notes;

        auto rendered = render::renderEditedAudio(input, [this, token] (float)
        {
            // より新しい要求が来ていたら中断する
            return ! juce::Thread::currentThreadShouldExit() && latestRequestToken.load() == token;
        });

        if (rendered.numChannels == 0)
            return; // 中断（新しい要求が処理される）

        newClip = std::make_shared<RenderedClip>();
        newClip->audio = std::move(rendered);
        newClip->revision = request.revision;

        for (const auto& channel : newClip->audio.channels)
            newClip->channelPointers.push_back(channel.data());
    }

    // 実行中に要求が取り消された・更新された場合は、古い結果を公開しない
    if (latestRequestToken.load() != token)
        return;

    publishClip(std::move(newClip), request.revision);
    renderPending = false;

    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        renderFinishedFlag = true;
    }

    if (wake)
        wake();

    // 高品質のニューラル加工（F-17）。先に TD-PSOLA の結果が再生されるため、音切れは出ない。
    if (request.settings.renderMode == 1 && audible)
    {
        runNeural(audio, request.notes, request.track, token, request.revision);
    }
    else
    {
        publishNeural(nullptr);
        neuralState = NeuralInfo::State::Unavailable; // 呼び出し側が探索結果から状態を決める
    }
}

// worker thread
void RenderEngine::runNeural(const std::shared_ptr<const SourceAudioData>& audio,
                             const std::vector<model::Note>& notes,
                             const model::PitchTrack& track,
                             std::uint64_t token,
                             std::uint64_t revision)
{
    const auto cancelled = [&] { return juce::Thread::currentThreadShouldExit() || latestRequestToken.load() != token; };

    // 編集が続いている間は始めない（新しい要求がなければ開始する）
    for (int waited = 0; waited < neuralDebounceMs.load(); waited += 50)
    {
        juce::Thread::sleep(50);

        if (cancelled())
            return;
    }

    neuralProgress = 0.0f;
    neuralState = NeuralInfo::State::Rendering;

    if (wake)
        wake();

    std::string error;
    auto vocoder = NeuralService::get().vocoder(error);

    if (vocoder == nullptr)
    {
        {
            const std::lock_guard<std::mutex> lock(messageMutex);
            neuralMessage = juce::String::fromUTF8(error.c_str());
        }
        neuralState = NeuralInfo::State::Failed;

        if (wake)
            wake();

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

        if (wake)
            wake();

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

    {
        const std::lock_guard<std::mutex> lock(handoffMutex);
        renderFinishedFlag = true;
    }

    if (wake)
        wake();
}

void RenderEngine::publishClip(std::shared_ptr<RenderedClip> newClip, std::uint64_t revision)
{
    std::shared_ptr<RenderedClip> previous;

    {
        render::ProcessingGate::WriteScope writeScope(gate);
        const std::lock_guard<std::mutex> lock(clipMutex);
        previous = std::move(clip);
        clip = std::move(newClip);
        published.store(clip.get(), std::memory_order_release);
        publishedRevision.store(revision, std::memory_order_release);
    }

    // previous はゲートの外で解放される
}

void RenderEngine::publishNeural(std::shared_ptr<RenderedClip> newClip)
{
    std::shared_ptr<RenderedClip> previous;

    {
        render::ProcessingGate::WriteScope writeScope(gate);
        const std::lock_guard<std::mutex> lock(clipMutex);
        previous = std::move(neuralClip);
        neuralClip = std::move(newClip);
        neuralPublished.store(neuralClip.get(), std::memory_order_release);
    }
}
} // namespace koenote::plugin
