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

    // --- フェーズ2（保存形式 v2 で追加）---
    // ノート全体のゲイン（dB。F-10）
    float gainDb = 0.0f;
    // フォルマントのシフト量（セント。+ で声が明るく・細くなる方向。F-09）。レンダリングは ±600 セントに制限する（TD-PSOLA では それ以上のシフトで基本周波数の聞こえ方が崩れるため）
    float formantShiftCents = 0.0f;
    // フォルマントをピッチ変更に追従させる割合（0 = 声質を保持、1 = ピッチと同じだけ動かす。F-09）
    float formantFollow = 0.0f;
    // このノートの直前の子音部（無声の立ち上がり）のゲイン（dB。F-10）
    float consonantGainDb = 0.0f;
    // このノートの直後から次のノートの子音部までの区間（ブレスなど）のゲイン（dB。F-10）
    float breathGainDb = 0.0f;

    bool hasTimingEdit() const noexcept { return targetStartSec >= 0.0 && targetEndSec >= 0.0; }

    // 既定値（音に影響する編集が何もない）か
    bool isDefault() const noexcept
    {
        return pitchOffsetCents == 0.0f && centerCorrection == 0.0f && vibratoScale == 1.0f && driftScale == 1.0f
               && ! hasTimingEdit() && gainDb == 0.0f && formantShiftCents == 0.0f && consonantGainDb == 0.0f
               && breathGainDb == 0.0f;
    }
};

// MIDI から取り込んだ目標ノート（F-12）。時刻は取り込み時のオフセットを適用した後の秒（編集後の時間軸）。
struct TargetNote
{
    double startSec = 0.0;
    double endSec = 0.0;
    float midi = 60.0f;
};

// 編集モデルに保存する設定（Undo の対象外）
struct ModelSettings
{
    // 0 = 標準（TD-PSOLA）、1 = ニューラル（F-17。利用できない環境では標準にフォールバック）
    std::uint8_t renderMode = 0;
    // スナップ設定（0 = なし、1 = 半音、2 = スケール）
    std::uint8_t snapMode = 1;
    std::uint8_t keyRoot = 0;   // 0 = C .. 11 = B
    std::uint8_t keyMinor = 0;  // 0 = メジャー、1 = マイナー
    // グリッドの分割（0 = 4 分、1 = 8 分、2 = 16 分、3 = 32 分音符）
    std::uint8_t gridDivision = 2;
    // ホストがテンポを提供しない場合に使う手動の BPM（0 = 使わない）
    float manualBpm = 0.0f;
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
