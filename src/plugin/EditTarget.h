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
    // 対象が破棄される直前に通知される。UI は参照を手放すこと。
    class DestructionListener
    {
    public:
        virtual ~DestructionListener() = default;
        virtual void editTargetWillBeDestroyed(EditTarget* target) = 0;
    };

    ~EditTarget() override = default;

    void addDestructionListener(DestructionListener* l) { destructionListeners.add(l); }
    void removeDestructionListener(DestructionListener* l) { destructionListeners.remove(l); }

    virtual model::EditModel& getModel() noexcept = 0;
    virtual const model::EditModel& getModel() const noexcept = 0;

    // モデルを編集した後に呼ぶ。レンダリングを要求し、UI とホストへ通知する。
    virtual void notifyEdited() = 0;

    virtual AnalysisState getAnalysisState() const noexcept = 0;
    virtual float getAnalysisProgress() const noexcept = 0;
    virtual bool isRenderPending() const noexcept = 0;

    // 元音声（編集前）。波形表示用。未読み込みなら nullptr。
    virtual std::shared_ptr<const SourceAudioData> getSourceAudio() const = 0;

    // 編集内容を破棄して、元音声から解析し直す
    virtual void requestReanalysis() = 0;

protected:
    // 派生クラスのデストラクタの先頭で呼ぶ
    void notifyWillBeDestroyed()
    {
        destructionListeners.call([this] (DestructionListener& l) { l.editTargetWillBeDestroyed(this); });
    }

private:
    juce::ListenerList<DestructionListener> destructionListeners;
};
} // namespace koenote::plugin
