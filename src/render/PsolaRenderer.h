#pragma once

#include "model/Note.h"
#include "model/PitchTrack.h"
#include "render/TimeMap.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace koenote::render
{
// レンダリング対象の元音声と編集内容（すべて参照。呼び出し中は有効であること）
struct RenderInput
{
    const float* const* channels = nullptr;
    int numChannels = 0;
    std::int64_t numFrames = 0;
    double sampleRate = 0.0;
    const model::PitchTrack* track = nullptr;
    const std::vector<model::Note>* notes = nullptr;
    TimeMap::Pins pins; // タイミング編集の範囲の固定
};

// レンダリング結果。出力時間軸（元音声の先頭 = 0）の音声。
struct RenderedAudio
{
    int numChannels = 0;
    double sampleRate = 0.0;
    std::vector<std::vector<float>> channels;

    std::int64_t numFrames() const noexcept
    {
        return channels.empty() ? 0 : static_cast<std::int64_t>(channels.front().size());
    }
};

// 進捗通知（0..1）。false を返すと中断する。
using RenderProgress = std::function<bool(float)>;

// 編集内容（ピッチ・タイミング）を反映するかを判定する。false なら元音声をそのまま使える。
bool hasAudibleEdits(const std::vector<model::Note>& notes) noexcept;

// TD-PSOLA によるピッチ変更とタイムストレッチ（要件 5章「レンダリング層」・標準方式）。
// ピッチマークはモノラル化した音声から求め、全チャンネルに共通で使う。
// 無編集の区間は元音声と一致する（窓の重みの和が 1 になる構成）。
// 中断された場合は numChannels == 0 の結果を返す。オーディオスレッドから呼んではならない。
RenderedAudio renderEditedAudio(const RenderInput& input, const RenderProgress& progress = {});
} // namespace koenote::render
