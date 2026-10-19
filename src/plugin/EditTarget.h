#pragma once

#include "model/EditModel.h"
#include "plugin/SourceAudioData.h"

#include <juce_events/juce_events.h>

#include <memory>

namespace koenote::plugin
{
enum class AnalysisState
{
    WaitingForAudio, // ホストからの音声取得待ち
    Analyzing,
    Done,
    Failed
};

// UI が編集する対象の抽象。実体は ARA の AudioModification（KoeNoteAudioModification）。
// ARA に依存しない UI のプレビュー・テストのために分離している。
// 変更の通知は ChangeBroadcaster で message thread に送る。
class EditTarget : public juce::ChangeBroadcaster
{
public:
    ~EditTarget() override = default;

    virtual model::EditModel& getModel() noexcept = 0;
    virtual const model::EditModel& getModel() const noexcept = 0;

    // モデルを編集した後に呼ぶ。レンダリングを要求し、UI とホストへ通知する。
    virtual void notifyEdited() = 0;

    virtual AnalysisState getAnalysisState() const noexcept = 0;
    virtual float getAnalysisProgress() const noexcept = 0;
    virtual bool isRenderPending() const noexcept = 0;

    // 元音声（編集前）。波形表示用。未読み込みなら nullptr。
    virtual std::shared_ptr<const SourceAudioData> getSourceAudio() const = 0;
};
} // namespace koenote::plugin
