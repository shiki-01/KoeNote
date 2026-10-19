#include "model/EditModel.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace koenote::model;
using Catch::Matchers::WithinAbs;

namespace
{
Note makeNote(double start, double end, float midi)
{
    Note n;
    n.startSec = start;
    n.endSec = end;
    n.detectedMidi = midi;
    return n;
}

PitchTrack makeTrack()
{
    PitchTrack t;
    t.hopSec = 0.01;
    t.f0Hz = { 0.0f, 220.0f, 221.0f, 0.0f };
    t.voicing = { 0.1f, 0.9f, 0.95f, 0.2f };
    return t;
}

EditModel makeModel()
{
    EditModel m;
    m.setAnalysis(makeTrack(), { makeNote(1.0, 2.0, 60.0f), makeNote(2.5, 3.5, 64.0f), makeNote(3.5, 4.0, 67.0f) });
    return m;
}
} // namespace

TEST_CASE("setAnalysis は id を採番し、開始時刻で整列する")
{
    EditModel m;
    m.setAnalysis(makeTrack(), { makeNote(2.0, 3.0, 62.0f), makeNote(0.5, 1.0, 60.0f) });

    REQUIRE(m.notes().size() == 2);
    CHECK(m.notes()[0].startSec == 0.5);
    CHECK(m.notes()[0].id != m.notes()[1].id);
    CHECK(m.hasAnalysis());
    CHECK_FALSE(m.canUndo());
}

TEST_CASE("ピッチ移動は Undo / Redo できる")
{
    auto m = makeModel();
    const auto id = m.notes()[0].id;

    REQUIRE(m.setNotePitchOffset(id, 200.0f));
    CHECK_THAT(m.findNote(id)->targetMidi(), WithinAbs(62.0, 1e-4));
    CHECK(m.canUndo());

    REQUIRE(m.undo());
    CHECK(m.findNote(id)->edit.pitchOffsetCents == 0.0f);
    CHECK(m.canRedo());

    REQUIRE(m.redo());
    CHECK(m.findNote(id)->edit.pitchOffsetCents == 200.0f);
}

TEST_CASE("同じ値の設定は履歴に積まれない")
{
    auto m = makeModel();
    const auto id = m.notes()[0].id;

    REQUIRE(m.setNotePitchOffset(id, 0.0f));
    CHECK_FALSE(m.canUndo());
}

TEST_CASE("beginEdit と endEdit で囲んだ複数の変更は 1 回の Undo で戻る")
{
    auto m = makeModel();
    const auto id = m.notes()[0].id;

    m.beginEdit();
    m.setNotePitchOffset(id, 50.0f);
    m.setNotePitchOffset(id, 100.0f);
    m.setNotePitchOffset(id, 150.0f);
    m.endEdit();

    REQUIRE(m.undo());
    CHECK(m.findNote(id)->edit.pitchOffsetCents == 0.0f);
    CHECK_FALSE(m.canUndo());
}

TEST_CASE("新しい編集を行うと Redo 履歴は破棄される")
{
    auto m = makeModel();
    const auto id = m.notes()[0].id;

    m.setNotePitchOffset(id, 100.0f);
    m.undo();
    REQUIRE(m.canRedo());
    m.setNotePitchOffset(id, 300.0f);
    CHECK_FALSE(m.canRedo());
}

TEST_CASE("ノートの分割は 2 つのノートを作り、結合で元に戻る")
{
    auto m = makeModel();
    const auto id = m.notes()[0].id;
    NoteId rightId = 0;

    REQUIRE(m.splitNote(id, 1.4, &rightId));
    REQUIRE(m.notes().size() == 4);
    CHECK(m.notes()[0].endSec == 1.4);
    CHECK(m.notes()[1].startSec == 1.4);
    CHECK(m.notes()[1].id == rightId);
    CHECK(m.notes()[1].endSec == 2.0);

    REQUIRE(m.mergeWithNext(id));
    REQUIRE(m.notes().size() == 3);
    CHECK(m.notes()[0].startSec == 1.0);
    CHECK(m.notes()[0].endSec == 2.0);
    CHECK_THAT(m.notes()[0].detectedMidi, WithinAbs(60.0, 1e-4));
}

TEST_CASE("端に近すぎる位置での分割は拒否される")
{
    auto m = makeModel();
    const auto id = m.notes()[0].id;
    CHECK_FALSE(m.splitNote(id, 1.01));
    CHECK_FALSE(m.splitNote(id, 1.99));
    CHECK(m.notes().size() == 3);
    CHECK_FALSE(m.canUndo());
}

TEST_CASE("結合したノートの目標ピッチは区間長で重み付けした平均になる")
{
    auto m = makeModel();
    const auto a = m.notes()[1].id; // 2.5-3.5 (64)
    const auto b = m.notes()[2].id; // 3.5-4.0 (67)

    m.setNotePitchOffset(a, 100.0f); // 65
    REQUIRE(m.mergeWithNext(a));

    // 目標: (65 * 1.0 + 67 * 0.5) / 1.5 = 65.666...
    const auto* merged = m.findNote(a);
    REQUIRE(merged != nullptr);
    CHECK_THAT(merged->targetMidi(), WithinAbs(65.6667, 1e-3));
    CHECK(m.findNote(b) == nullptr);
}

TEST_CASE("境界の移動は隣のノートと整合する範囲でのみ許される")
{
    auto m = makeModel();
    const auto id = m.notes()[1].id; // 2.5-3.5 と 3.5-4.0 の境界

    REQUIRE(m.moveBoundary(id, 3.7));
    CHECK(m.notes()[1].endSec == 3.7);
    CHECK(m.notes()[2].startSec == 3.7);

    // 後ろのノートの長さが最小値を下回る位置は拒否
    CHECK_FALSE(m.moveBoundary(id, 3.99));
    // 前のノートの開始より前も拒否
    CHECK_FALSE(m.moveBoundary(id, 2.4));
    CHECK(m.notes()[1].endSec == 3.7);
}

TEST_CASE("タイミング編集は隣のノートと重ならない範囲に制限される")
{
    auto m = makeModel();
    const auto id = m.notes()[0].id; // 1.0-2.0、次は 2.5 開始

    REQUIRE(m.setNoteTiming(id, 1.2, 2.4));
    CHECK(m.findNote(id)->edit.hasTimingEdit());
    CHECK(m.findNote(id)->outEndSec() == 2.4);

    CHECK_FALSE(m.setNoteTiming(id, 1.2, 2.6)); // 次のノートに食い込む
    CHECK_FALSE(m.setNoteTiming(id, 1.2, 1.21)); // 短すぎる
    CHECK(m.findNote(id)->outEndSec() == 2.4);
}

TEST_CASE("タイミングを元の区間に戻すと編集なしの状態になる")
{
    auto m = makeModel();
    const auto id = m.notes()[0].id;

    m.setNoteTiming(id, 1.2, 2.3);
    REQUIRE(m.setNoteTiming(id, 1.0, 2.0));
    CHECK_FALSE(m.findNote(id)->edit.hasTimingEdit());
}

TEST_CASE("タイミング編集済みノートの分割は出力側も同じ比率で分割する")
{
    auto m = makeModel();
    const auto id = m.notes()[0].id;
    m.setNoteTiming(id, 1.0, 2.4); // 1.4 秒 → 出力 1.4 秒（実際は 1.0 → 1.4 倍）

    NoteId right = 0;
    REQUIRE(m.splitNote(id, 1.5, &right)); // 元の区間の 50% 地点
    CHECK_THAT(m.findNote(id)->edit.targetEndSec, WithinAbs(1.7, 1e-9));
    CHECK_THAT(m.findNote(right)->edit.targetStartSec, WithinAbs(1.7, 1e-9));
}

TEST_CASE("resetNote は編集内容を初期状態に戻す")
{
    auto m = makeModel();
    const auto id = m.notes()[0].id;
    m.setNotePitchOffset(id, 100.0f);
    m.setNoteTiming(id, 1.1, 2.1);

    REQUIRE(m.resetNote(id));
    CHECK(m.findNote(id)->edit.pitchOffsetCents == 0.0f);
    CHECK_FALSE(m.findNote(id)->edit.hasTimingEdit());
}

TEST_CASE("存在しない id への操作は失敗する")
{
    auto m = makeModel();
    CHECK_FALSE(m.setNotePitchOffset(9999, 10.0f));
    CHECK_FALSE(m.splitNote(9999, 1.5));
    CHECK_FALSE(m.mergeWithNext(9999));
    CHECK_FALSE(m.moveBoundary(9999, 1.5));
}

TEST_CASE("revision は変更のたびに増え、失敗した操作では増えない")
{
    auto m = makeModel();
    const auto id = m.notes()[0].id;
    const auto r0 = m.revision();

    m.setNotePitchOffset(id, 100.0f);
    const auto r1 = m.revision();
    CHECK(r1 > r0);

    m.splitNote(id, 1.0001);
    CHECK(m.revision() == r1);

    m.undo();
    CHECK(m.revision() > r1);
}

TEST_CASE("シリアライズしたデータを復元すると同じ内容になる")
{
    auto m = makeModel();
    const auto id = m.notes()[0].id;
    m.setNotePitchOffset(id, -123.0f);
    m.setNoteTiming(id, 1.1, 2.2);
    NoteEdit e = m.findNote(m.notes()[1].id)->edit;
    e.vibratoScale = 0.5f;
    e.driftScale = 0.25f;
    e.centerCorrection = 0.8f;
    e.transitionSmoothness = 0.3f;
    m.setNoteEdit(m.notes()[1].id, e);

    const auto bytes = m.serialize();
    const auto restored = EditModel::deserialize(bytes.data(), bytes.size());

    REQUIRE(restored.has_value());
    REQUIRE(restored->notes().size() == m.notes().size());
    CHECK(restored->hasAnalysis());
    CHECK(restored->pitchTrack().f0Hz == m.pitchTrack().f0Hz);
    CHECK(restored->pitchTrack().voicing == m.pitchTrack().voicing);

    for (std::size_t i = 0; i < m.notes().size(); ++i)
    {
        const auto& a = m.notes()[i];
        const auto& b = restored->notes()[i];
        CHECK(a.id == b.id);
        CHECK(a.startSec == b.startSec);
        CHECK(a.endSec == b.endSec);
        CHECK(a.detectedMidi == b.detectedMidi);
        CHECK(a.edit.pitchOffsetCents == b.edit.pitchOffsetCents);
        CHECK(a.edit.vibratoScale == b.edit.vibratoScale);
        CHECK(a.edit.driftScale == b.edit.driftScale);
        CHECK(a.edit.centerCorrection == b.edit.centerCorrection);
        CHECK(a.edit.transitionSmoothness == b.edit.transitionSmoothness);
        CHECK(a.edit.targetStartSec == b.edit.targetStartSec);
        CHECK(a.edit.targetEndSec == b.edit.targetEndSec);
    }
}

TEST_CASE("復元したモデルでの新規ノート id は既存の id と衝突しない")
{
    auto m = makeModel();
    const auto bytes = m.serialize();
    auto restored = EditModel::deserialize(bytes.data(), bytes.size());
    REQUIRE(restored.has_value());

    NoteId right = 0;
    REQUIRE(restored->splitNote(restored->notes()[0].id, 1.5, &right));
    for (const auto& n : restored->notes())
        if (n.id == right)
            continue;
        else
            CHECK(n.id != right);
}

TEST_CASE("保存データの先頭にはマジックとバージョンが書かれる")
{
    const auto bytes = makeModel().serialize();
    REQUIRE(bytes.size() > 8);
    CHECK(bytes[0] == 'K');
    CHECK(bytes[1] == 'N');
    CHECK(bytes[2] == 'E');
    CHECK(bytes[3] == 'M');
    CHECK(bytes[4] == kEditModelFormatVersion);
}

TEST_CASE("未来のバージョンで保存されたデータは拒否される")
{
    auto bytes = makeModel().serialize();
    bytes[4] = static_cast<std::uint8_t>(kEditModelFormatVersion + 1);
    CHECK_FALSE(EditModel::deserialize(bytes.data(), bytes.size()).has_value());
}

TEST_CASE("壊れた保存データは拒否される")
{
    const auto bytes = makeModel().serialize();

    // 途中で切れたデータ
    for (const std::size_t cut : { std::size_t { 0 }, std::size_t { 3 }, std::size_t { 12 }, bytes.size() / 2, bytes.size() - 1 })
        CHECK_FALSE(EditModel::deserialize(bytes.data(), cut).has_value());

    // マジックが違う
    auto bad = bytes;
    bad[0] = 'X';
    CHECK_FALSE(EditModel::deserialize(bad.data(), bad.size()).has_value());
}

TEST_CASE("巨大な要素数を持つ保存データでメモリを確保しない")
{
    auto bytes = makeModel().serialize();
    // フレーム数フィールド（magic 4 + version 4 + analysed 1 + nextId 8 + start 8 + hop 8 = 33 バイト目）を巨大値にする
    for (int i = 0; i < 8; ++i)
        bytes[33 + i] = 0xFF;
    CHECK_FALSE(EditModel::deserialize(bytes.data(), bytes.size()).has_value());
}

// バージョン 1 の保存形式の固定。形式を変更したときはこのテストが失敗する（意図した変更なら
// 旧形式の読み込み処理を追加したうえで、新しいバージョン用のテストを足すこと）。
TEST_CASE("バージョン 1 の保存データ（固定バイト列）を読み込める")
{
    EditModel m;
    PitchTrack t;
    t.startSec = 0.0;
    t.hopSec = 0.5;
    t.f0Hz = { 100.0f };
    t.voicing = { 1.0f };
    m.setAnalysis(t, { makeNote(1.0, 2.0, 60.0f) });
    const auto bytes = m.serialize();

    // サイズ: ヘッダ 4+4+1+8 + トラック 8+8+8+4+4 + ノート数 8 + ノート 68
    CHECK(bytes.size() == 17 + 32 + 8 + 68);

    const auto restored = EditModel::deserialize(bytes.data(), bytes.size());
    REQUIRE(restored.has_value());
    CHECK(restored->notes().size() == 1);
    CHECK(restored->pitchTrack().f0Hz.size() == 1);
}
