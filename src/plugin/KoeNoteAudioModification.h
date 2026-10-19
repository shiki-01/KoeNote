#pragma once

#include "model/EditModel.h"
#include "plugin/EditTarget.h"
#include "plugin/KoeNoteAudioSource.h"
#include "plugin/RenderEngine.h"
#include "render/ProcessingGate.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

namespace koenote::analysis
{
struct AlignmentResult;
}

namespace koenote::plugin
{
// ARAAudioModification の拡張。編集モデルの保持と、ARA 固有の処理（ホストへの通知、テンポ・リージョンの情報）を担当する。
// 解析・整列・レンダリングは RenderEngine（worker thread）が行う。
//
// スレッド:
//  - message thread: getModel() の操作、notifyEdited()、ChangeBroadcaster の通知、ARA モデルへのアクセス
//  - audio thread  : viewsForPlayback() だけ（ProcessingGate の ReadScope 内）
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
    void requestReanalysis() override;

    AnalysisState getAnalysisState() const noexcept override { return analysisState.load(); }
    float getAnalysisProgress() const noexcept override { return engine.analysisProgress(); }
    bool isRenderPending() const noexcept override { return engine.isRenderPending(); }

    // 元音声（編集前）。UI の波形表示用。未読み込みなら nullptr。
    std::shared_ptr<const SourceAudioData> getSourceAudio() const override;

    model::TempoMap getTempoMap() const override;
    double getTimeOffsetSec() const override;

    void setCompare(const CompareState& state) override;
    CompareState getCompare() const override { return engine.getCompare(); }

    std::vector<GuideCandidate> getGuideCandidates() const override;
    void selectGuide(int guideId) override;
    void selectGuideAudio(std::shared_ptr<const SourceAudioData> audio, const juce::String& name) override;
    void clearGuide() override;
    void setGuideOffset(double offsetSec) override;
    GuideState getGuide() const override;
    void alignToGuide() override;
    AlignInfo getAlignInfo() const override { return engine.alignInfo(); }

    NeuralInfo getNeuralInfo() const override;

    //==========================================================================
    // audio thread（ProcessingGate の ReadScope 内でのみ呼ぶ）
    using PlaybackAudio = RenderEngine::PlaybackAudio;
    using PlaybackViews = RenderEngine::PlaybackViews;

    PlaybackViews viewsForPlayback() const noexcept { return engine.viewsForPlayback(); }

private:
    void handleAsyncUpdate() override;
    void requestRender();
    void applyAlignment(const analysis::AlignmentResult& result);
    model::TempoMap readHostTempo() const;

    model::EditModel model;
    RenderEngine engine;

    // ガイド（message thread のみ）
    std::shared_ptr<const SourceAudioData> guideAudio;
    std::shared_ptr<const GuideAnalysis> guideAnalysis;
    juce::String guideName;
    double guideOffset = 0.0;

    std::atomic<AnalysisState> analysisState { AnalysisState::WaitingForAudio };
    std::atomic<bool> startRequested { false };

    // ホストのテンポ（読み取りは負荷があるため、短時間キャッシュする）
    mutable model::TempoMap hostTempo;
    mutable juce::int64 hostTempoReadAt = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KoeNoteAudioModification)
};
} // namespace koenote::plugin
