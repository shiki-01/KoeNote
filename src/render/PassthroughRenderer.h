#pragma once

#include <cstdint>

namespace koenote::render
{
// 非インターリーブの float 音声への参照（所有しない）
struct AudioView
{
    const float* const* channels = nullptr;
    int numChannels = 0;
    std::int64_t numFrames = 0;
};

// src の [srcStart, srcStart + numFrames) を dst の dstOffset 以降へ「加算」する（加工なしのパススルー）。
// src の範囲外は無音として扱う。チャンネル数が異なる場合は次のように対応付ける。
//  - src が dst より少ない: 最後のチャンネルを複製（モノラル → ステレオなど）
//  - dst がモノラルで src が複数: 全チャンネルの平均
// オーディオスレッドから呼べる（メモリ確保・ロックなし）。dst は呼び出し側で 0 クリアしておくこと。
void accumulatePassthrough(const AudioView& src,
                           std::int64_t srcStart,
                           float* const* dst,
                           int numDstChannels,
                           int dstOffset,
                           int numFrames) noexcept;
} // namespace koenote::render
