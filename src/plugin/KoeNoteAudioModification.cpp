#include "plugin/KoeNoteAudioModification.h"

#include "analysis/DtwAligner.h"
#include "plugin/NeuralService.h"

#include <algorithm>

namespace koenote::plugin
{
namespace
{
juce::int64 nowMs() { return static_cast<juce::int64>(juce::Time::getMillisecondCounter()); }
} // namespace

KoeNoteAudioModification::KoeNoteAudioModification(juce::ARAAudioSource* source,
                                                   ARA::ARAAudioModificationHostRef hostRef,
                                                   const juce::ARAAudioModification* modificationToClone,
                                                   render::ProcessingGate* gateIn)
    : juce::ARAAudioModification(source, hostRef, modificationToClone),
      engine(*gateIn, *static_cast<KoeNoteAudioSource*>(source), [this] { triggerAsyncUpdate(); })
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
    engine.stop(); // worker thread を先に止め、その後で保留中の更新通知を取り消す
    cancelPendingUpdate();
}

std::shared_ptr<const SourceAudioData> KoeNoteAudioModification::getSourceAudio() const
{
    return static_cast<const KoeNoteAudioSource*>(getAudioSource())->sharedAudio();
}

//==============================================================================
void KoeNoteAudioModification::onSourceAudioReady()
{
    if (! model.hasAnalysis())
    {
        analysisState = AnalysisState::Analyzing;
        engine.requestAnalysis();
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
    engine.resetRendering();

    if (getAudioSource<KoeNoteAudioSource>()->isLoaded())
        onSourceAudioReady();

    sendChangeMessage();
}

void KoeNoteAudioModification::notifyEdited()
{
    requestRender();
    sendChangeMessage();
}

void KoeNoteAudioModification::requestRender()
{
    if (! model.hasAnalysis())
        return;

    RenderEngine::RenderRequest request;
    request.notes = model.notes();
    request.track = model.pitchTrack();
    request.revision = model.revision();
    request.settings = model.settings();
    engine.requestRender(std::move(request));
}

//==============================================================================
// テンポ・時間のずれ（F-14）
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
    engine.setCompare(state);
    sendChangeMessage();
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

void KoeNoteAudioModification::alignToGuide(int guideId, double startOffsetSec)
{
    int index = 0;

    for (auto* source : getAudioSource()->getDocument()->getAudioSources<KoeNoteAudioSource>())
    {
        if (index++ == guideId)
        {
            alignToGuideAudio(source->sharedAudio(), startOffsetSec);
            return;
        }
    }
}

void KoeNoteAudioModification::alignToGuideAudio(std::shared_ptr<const SourceAudioData> guide, double startOffsetSec)
{
    if (guide == nullptr || ! model.hasAnalysis() || getSourceAudio() == nullptr)
    {
        engine.markAlignFailed();
        sendChangeMessage();
        return;
    }

    engine.requestAlign(std::move(guide), startOffsetSec);
    sendChangeMessage();
}

void KoeNoteAudioModification::applyAlignment(const analysis::AlignmentResult& result)
{
    if (! result.valid || ! model.hasAnalysis())
    {
        engine.markAlignFailed();
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
        engine.markAlignDone();
        notifyEdited();
    }
    else
    {
        engine.markAlignFailed();
    }
}

//==============================================================================
NeuralInfo KoeNoteAudioModification::getNeuralInfo() const
{
    auto info = engine.neuralInfo();

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
// message thread
void KoeNoteAudioModification::handleAsyncUpdate()
{
    if (startRequested.exchange(false))
    {
        if (getAudioSource<KoeNoteAudioSource>()->isLoaded())
            onSourceAudioReady();
    }

    auto events = engine.takeEvents();

    if (events.analysis.has_value())
    {
        // 解析中にモデルが復元された場合は、そちらを優先する
        if (! model.hasAnalysis())
            model.setAnalysis(std::move(events.analysis->track), std::move(events.analysis->notes));

        analysisState = AnalysisState::Done;
        sendChangeMessage();
    }

    if (events.analysisFailed)
    {
        analysisState = AnalysisState::Failed;
        sendChangeMessage();
    }

    if (events.alignment != nullptr)
    {
        applyAlignment(*events.alignment);
        sendChangeMessage();
    }

    if (events.renderFinished)
    {
        // 再生系（ホスト）へ、サンプルが変わったことを知らせる
        notifyContentChanged(juce::ARAContentUpdateScopes::samplesAreAffected(), true);

        for (auto* region : getPlaybackRegions())
            region->notifyContentChanged(juce::ARAContentUpdateScopes::samplesAreAffected(), true);

        sendChangeMessage();
    }

    // ニューラルの進捗表示の更新
    if (const auto state = engine.neuralInfo().state; state == NeuralInfo::State::Rendering || state == NeuralInfo::State::Failed)
        sendChangeMessage();
}
} // namespace koenote::plugin
