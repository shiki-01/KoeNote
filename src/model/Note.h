#pragma once

#include <cstdint>

namespace koenote::model
{
using NoteId = std::uint64_t;

// ノート単位の編集パラメータ（要件 F-04〜F-06）。既定値は「無編集」を表す。
struct NoteEdit
{
    // 検出ピッチ中心からのピッチ移動量（セント）。ドラッグ移動・スナップの結果が入る。
    float pitchOffsetCents = 0.0f;
    // ピッチ中心の補正量（0 = 元のピッチ揺れを保持、1 = ノート内のピッチ中心を目標に完全に揃える）
    float centerCorrection = 0.0f;
    // ビブラート量の倍率（1 = 元のまま、0 = ビブラート除去）
    float vibratoScale = 1.0f;
    // ピッチドリフト（ゆるやかなピッチ変動）の倍率
    float driftScale = 1.0f;
    // ノート境界付近のピッチ遷移の滑らかさ（0..1）
    float transitionSmoothness = 0.0f;
    // 編集後のノート区間（source time ではなく、出力時間軸上の秒）。
    // 負値の場合は元の区間と同じ（タイミング無編集）を表す。
    double targetStartSec = -1.0;
    double targetEndSec = -1.0;

    bool hasTimingEdit() const noexcept { return targetStartSec >= 0.0 && targetEndSec >= 0.0; }
};

// 解析で得られたノート（元音声の時間軸）と、その編集内容
struct Note
{
    NoteId id = 0;
    double startSec = 0.0;   // 元音声上の開始時刻（秒）
    double endSec = 0.0;     // 元音声上の終了時刻（秒）
    float detectedMidi = 60.0f; // 検出したピッチ中心（MIDI ノート番号、小数）
    float level = 0.0f;         // 平均音量（0..1 の RMS）
    NoteEdit edit;

    // 編集後のピッチ中心（MIDI ノート番号）
    float targetMidi() const noexcept { return detectedMidi + edit.pitchOffsetCents / 100.0f; }
    double outStartSec() const noexcept { return edit.hasTimingEdit() ? edit.targetStartSec : startSec; }
    double outEndSec() const noexcept { return edit.hasTimingEdit() ? edit.targetEndSec : endSec; }
};
} // namespace koenote::model
