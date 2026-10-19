#pragma once

#include "plugin/SourceAudioData.h"
#include "render/ProcessingGate.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace koenote::plugin
{
// ARAAudioSource の拡張。サンプルアクセスが有効になった時点で、別スレッドが全サンプルを読み込む。
// オーディオスレッドは読み込み済みのデータだけを参照し、ホストの reader には触れない。
class KoeNoteAudioSource final : public juce::ARAAudioSource,
                                 private juce::AsyncUpdater
{
public:
    // LoadThread が不完全型のため、コンストラクタ・デストラクタは .cpp で定義する
    KoeNoteAudioSource(ARA::PlugIn::Document* document, ARA::ARAAudioSourceHostRef hostRef);
    ~KoeNoteAudioSource() override;

    void setGate(render::ProcessingGate* gateIn) noexcept { gate = gateIn; }

    // 読み込み完了時に message thread で呼ばれる
    std::function<void(KoeNoteAudioSource&)> onLoaded;

    // message thread 用。読み込みを（再）開始する。進行中の読み込みは破棄される。
    void startLoading();
    // message thread 用。読み込みを中断し、スレッドの終了を待つ。
    void stopLoading();

    // オーディオスレッド用。ProcessingGate の ReadScope 内でのみ使うこと。未読み込みなら nullptr。
    const SourceAudioData* audioForPlayback() const noexcept { return published.load(std::memory_order_acquire); }

    // message thread / worker thread 用。所有権を共有して取得する。
    std::shared_ptr<const SourceAudioData> sharedAudio() const;

    bool isLoaded() const noexcept { return published.load(std::memory_order_acquire) != nullptr; }

private:
    class LoadThread;

    void handleAsyncUpdate() override;
    void publish(std::shared_ptr<SourceAudioData> newData);

    render::ProcessingGate* gate = nullptr;
    std::unique_ptr<LoadThread> loader;

    mutable std::mutex dataMutex;
    std::shared_ptr<SourceAudioData> data;
    std::atomic<const SourceAudioData*> published { nullptr };
};
} // namespace koenote::plugin
