#pragma once

#include "model/EditModel.h"
#include "model/TempoMap.h"
#include "plugin/SourceAudioData.h"

#include <juce_events/juce_events.h>

#include <memory>
#include <vector>

namespace koenote::plugin
{
enum class AnalysisState
{
    WaitingForAudio, // ホストからの音声取得待ち
    Analyzing,
    Done,
    Failed
};

// A/B 比較・バイパス（F-18）。useOriginal が true の間、元の音声を再生する。
// rangeOnly が true なら、[startSec, endSec]（AudioModification の時間）の範囲だけ元の音声にする。
struct CompareState
{
    bool useOriginal = false;
    bool rangeOnly = false;
    double startSec = 0.0;
    double endSec = 0.0;
};

// 整列（F-13）のガイドにできる、ドキュメント内の別の音声
struct GuideCandidate
{
    int id = 0;
    juce::String name;
    double durationSec = 0.0;
};

// ガイド（リードボーカルなど）の解析結果。ピアノロールの背景に表示し、そこに合わせて編集する。
// 時刻はガイド自身の音声の時間軸（秒）。
struct GuideAnalysis
{
    model::PitchTrack track;
    std::vector<model::Note> notes;
    std::shared_ptr<const SourceAudioData> audio;
    double durationSec = 0.0;
};

struct GuideState
{
    bool active = false;                       // ガイドが選ばれている
    bool analyzing = false;                    // 解析中（analysis はまだない）
    juce::String name;
    std::shared_ptr<const GuideAnalysis> analysis;
    // ガイド上の時刻 = このクリップの時刻 + offsetSec。背景のガイドは、時刻 g を「g - offsetSec」の位置に描く。
    double offsetSec = 0.0;
};

struct AlignInfo
{
    enum class State { Idle, Working, Done, Failed };
    State state = State::Idle;
    float progress = 0.0f;
};

// ニューラル加工（F-17）の状態
struct NeuralInfo
{
    enum class State { Unavailable, Idle, Rendering, Ready, Failed };
    State state = State::Unavailable;
    float progress = 0.0f;
    juce::String message; // 利用できない理由、失敗の理由、読み込んだボコーダー名など
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

    //==========================================================================
    // フェーズ2。既定の実装は「機能なし」を表す（プレビュー用の実装が最小限で済むように）。

    // ホストのテンポ情報（F-14）。提供されなければ無効な TempoMap。
    virtual model::TempoMap getTempoMap() const { return {}; }
    // 編集の時間軸（AudioModification の時間）→ プロジェクトの時刻（秒）のずれ
    virtual double getTimeOffsetSec() const { return 0.0; }

    // A/B 比較・バイパス（F-18）
    virtual void setCompare(const CompareState&) {}
    virtual CompareState getCompare() const { return {}; }

    // タイミング整列（F-13）。ガイドの音声との DTW で、各ノートのタイミングを合わせる。
    virtual std::vector<GuideCandidate> getGuideCandidates() const { return {}; }
    // ガイドの選択（ドキュメント内の別の音声、または読み込んだ音声）。解析結果は背景に表示される。
    virtual void selectGuide(int /*guideId*/) {}
    virtual void selectGuideAudio(std::shared_ptr<const SourceAudioData> /*audio*/, const juce::String& /*name*/) {}
    virtual void clearGuide() {}
    // 背景のガイドの開始位置をずらす（ガイド上の時刻 = このクリップの時刻 + offsetSec）
    virtual void setGuideOffset(double /*offsetSec*/) {}
    virtual GuideState getGuide() const { return {}; }
    // 選択中のガイドへ、現在のずれから DTW で自動整列する
    virtual void alignToGuide() {}
    virtual AlignInfo getAlignInfo() const { return {}; }

    // ニューラル加工（F-17）
    virtual NeuralInfo getNeuralInfo() const { return {}; }

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
