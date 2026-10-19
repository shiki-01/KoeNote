// スケールのスナップ、テンポマップ（F-14）、一括補正（F-16）。

#include "model/BatchCorrection.h"
#include "model/EditModel.h"
#include "model/Scale.h"
#include "model/TempoMap.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace koenote::model;
using Catch::Matchers::WithinAbs;

namespace
{
Note makeNote(NoteId id, double start, double end, float midi)
{
    Note n;
    n.id = id;
    n.startSec = start;
    n.endSec = end;
    n.detectedMidi = midi;
    return n;
}
} // namespace

TEST_CASE("スケールの構成音（C メジャー・A マイナー）")
{
    CHECK(isInScale(0, 0, ScaleType::Major));
    CHECK(isInScale(4, 0, ScaleType::Major));
    CHECK_FALSE(isInScale(1, 0, ScaleType::Major));
    CHECK_FALSE(isInScale(6, 0, ScaleType::Major));

    CHECK(isInScale(9, 9, ScaleType::Minor));
    CHECK(isInScale(0, 9, ScaleType::Minor)); // C は A マイナーの構成音
    CHECK_FALSE(isInScale(8, 9, ScaleType::Minor)); // G# は構成音ではない（自然短音階）
}

TEST_CASE("スナップは最も近い音へ丸める")
{
    CHECK(snapMidi(60.4f, SnapMode::Off, 0, ScaleType::Major) == 60.4f);
    CHECK(snapMidi(60.4f, SnapMode::Semitone, 0, ScaleType::Major) == 60.0f);
    CHECK(snapMidi(60.6f, SnapMode::Semitone, 0, ScaleType::Major) == 61.0f);

    // C メジャーでは 61.2 は C# ではなく D（62）へ（C まで 1.2、D まで 0.8）
    CHECK(snapMidi(61.2f, SnapMode::Scale, 0, ScaleType::Major) == 62.0f);
    // 負の MIDI 値や遠い値でも構成音に収まる
    CHECK(snapMidi(1.3f, SnapMode::Scale, 0, ScaleType::Major) == 2.0f);
}

TEST_CASE("一定テンポの TempoMap は四分音符と秒を相互に変換する")
{
    const auto t = TempoMap::constant(120.0); // 四分音符 = 0.5 秒
    REQUIRE(t.valid());

    CHECK_THAT(t.secToQuarter(1.0), WithinAbs(2.0, 1e-9));
    CHECK_THAT(t.quarterToSec(3.0), WithinAbs(1.5, 1e-9));
    CHECK_THAT(t.bpmAt(10.0), WithinAbs(120.0, 1e-6));
    // 範囲外は外挿
    CHECK_THAT(t.secToQuarter(-1.0), WithinAbs(-2.0, 1e-9));
    CHECK_THAT(t.secToQuarter(5000.0), WithinAbs(10000.0, 1e-6));
}

TEST_CASE("テンポ変化のある TempoMap")
{
    // 0〜2 秒は 4 四分音符（120 bpm）、2〜3 秒は 4 四分音符（240 bpm）
    const TempoMap t({ { 0.0, 0.0 }, { 2.0, 4.0 }, { 3.0, 8.0 } });
    REQUIRE(t.valid());

    CHECK_THAT(t.secToQuarter(2.5), WithinAbs(6.0, 1e-9));
    CHECK_THAT(t.quarterToSec(6.0), WithinAbs(2.5, 1e-9));
    CHECK_THAT(t.bpmAt(1.0), WithinAbs(120.0, 1e-6));
    CHECK_THAT(t.bpmAt(2.5), WithinAbs(240.0, 1e-6));
}

TEST_CASE("不正な TempoMap は無効になる")
{
    CHECK_FALSE(TempoMap().valid());
    CHECK_FALSE(TempoMap({ { 0.0, 0.0 } }).valid());
    CHECK_FALSE(TempoMap::constant(0.0).valid());
    CHECK_FALSE(TempoMap::constant(-10.0).valid());
    // 増加しない点は捨てられる
    CHECK_FALSE(TempoMap({ { 0.0, 0.0 }, { 0.0, 1.0 } }).valid());
}

TEST_CASE("グリッドへのスナップ（16 分音符、120 bpm なら 0.125 秒刻み）")
{
    const auto t = TempoMap::constant(120.0);
    CHECK_THAT(snapToGrid(0.13, t, 0.0, 0.25), WithinAbs(0.125, 1e-9));
    CHECK_THAT(snapToGrid(0.19, t, 0.0, 0.25), WithinAbs(0.25, 1e-9));
    // リージョンがプロジェクトの 0.05 秒から始まる場合、編集時間 0.1 はプロジェクトの 0.15 秒 → 0.125 へ → 編集時間 0.075
    CHECK_THAT(snapToGrid(0.1, t, 0.05, 0.25), WithinAbs(0.075, 1e-9));
    // 無効な TempoMap は変更しない
    CHECK(snapToGrid(0.13, TempoMap(), 0.0, 0.25) == 0.13);
}

TEST_CASE("一括補正: ピッチを半音へ寄せる強さ")
{
    std::vector<Note> notes { makeNote(1, 0.0, 1.0, 60.3f), makeNote(2, 1.0, 2.0, 61.8f) };

    BatchParams p;
    p.pitchStrength = 1.0f;
    p.snapMode = SnapMode::Semitone;
    p.vibratoKeep = 0.25f;

    auto edits = computeBatchEdits(notes, {}, p);
    REQUIRE(edits.size() == 2);
    CHECK_THAT(static_cast<double>(edits[0].second.pitchOffsetCents), WithinAbs(-30.0, 1e-3));
    CHECK_THAT(static_cast<double>(edits[1].second.pitchOffsetCents), WithinAbs(20.0, 1e-3));
    CHECK(edits[0].second.vibratoScale == 0.25f);
    CHECK(edits[0].second.centerCorrection == 1.0f);

    p.pitchStrength = 0.5f;
    edits = computeBatchEdits(notes, {}, p);
    CHECK_THAT(static_cast<double>(edits[0].second.pitchOffsetCents), WithinAbs(-15.0, 1e-3));
    CHECK(edits[0].second.centerCorrection == 0.5f);

    p.pitchStrength = 0.0f;
    edits = computeBatchEdits(notes, {}, p);
    CHECK(edits[0].second.pitchOffsetCents == 0.0f);
}

TEST_CASE("一括補正: 対象の id を指定できる")
{
    std::vector<Note> notes { makeNote(1, 0.0, 1.0, 60.3f), makeNote(2, 1.0, 2.0, 61.8f) };
    BatchParams p;

    const auto edits = computeBatchEdits(notes, { 2 }, p);
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].first == 2);
}

TEST_CASE("一括補正: タイミングをグリッドへ寄せる（長さは保つ）")
{
    std::vector<Note> notes { makeNote(1, 0.13, 0.5, 60.0f) };

    BatchParams p;
    p.pitchStrength = 0.0f;
    p.timingStrength = 1.0f;
    p.gridQuarters = 0.25;
    p.tempo = TempoMap::constant(120.0);

    auto edits = computeBatchEdits(notes, {}, p);
    REQUIRE(edits.size() == 1);
    CHECK_THAT(edits[0].second.targetStartSec, WithinAbs(0.125, 1e-9));
    CHECK_THAT(edits[0].second.targetEndSec, WithinAbs(0.125 + 0.37, 1e-9));

    // 終了位置も寄せる
    p.keepDuration = false;
    edits = computeBatchEdits(notes, {}, p);
    CHECK_THAT(edits[0].second.targetEndSec, WithinAbs(0.5, 1e-9));

    // 強さ 0.5 なら半分だけ
    p.keepDuration = true;
    p.timingStrength = 0.5f;
    edits = computeBatchEdits(notes, {}, p);
    CHECK_THAT(edits[0].second.targetStartSec, WithinAbs(0.13 + (0.125 - 0.13) * 0.5, 1e-9));

    // テンポが無効ならタイミングは変更しない
    p.tempo = TempoMap();
    edits = computeBatchEdits(notes, {}, p);
    CHECK_FALSE(edits[0].second.hasTimingEdit());
}

TEST_CASE("一括補正の結果を EditModel に適用すると、重なりは押し出され 1 回の Undo で戻る")
{
    EditModel m;
    PitchTrack t;
    t.f0Hz = { 0.0f };
    t.voicing = { 0.0f };
    // 0.13-0.5 と、すぐ後ろの 0.52-0.9（グリッド寄せで前のノートの終端と重なる）
    m.setAnalysis(t, { makeNote(0, 0.13, 0.5, 60.3f), makeNote(0, 0.52, 0.9, 62.0f) });

    BatchParams p;
    p.timingStrength = 1.0f;
    p.gridQuarters = 0.25;
    p.tempo = TempoMap::constant(120.0);
    p.pitchStrength = 1.0f;

    const auto edits = computeBatchEdits(m.notes(), {}, p);
    REQUIRE(m.applyEdits(edits));

    const auto& notes = m.notes();
    CHECK_THAT(static_cast<double>(notes[0].edit.pitchOffsetCents), WithinAbs(-30.0, 1e-3));
    CHECK(notes[1].outStartSec() >= notes[0].outEndSec() - 1e-9);

    REQUIRE(m.undo());
    CHECK(m.notes()[0].edit.pitchOffsetCents == 0.0f);
    CHECK_FALSE(m.canUndo());
}
