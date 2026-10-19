#pragma once

#include "plugin/SourceAudioData.h"

#include <memory>

namespace koenote::plugin
{
// 元音声の提供元（ホストから読み込んだ全サンプル）。ARA の AudioSource が実装する。
// ARA に依存しない RenderEngine を単体でテストできるように分離している。
class ISourceAudio
{
public:
    virtual ~ISourceAudio() = default;

    // 所有権を共有して取得する（message thread / worker thread）。未読み込みなら nullptr。
    virtual std::shared_ptr<const SourceAudioData> sharedAudio() const = 0;

    // オーディオスレッド用。ProcessingGate の ReadScope 内でのみ使う。未読み込みなら nullptr。
    virtual const SourceAudioData* audioForPlayback() const noexcept = 0;
};
} // namespace koenote::plugin
