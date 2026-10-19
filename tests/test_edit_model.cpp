#include "model/ByteStream.h"
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

// バージョン 1 の保存データ（フェーズ1 で保存したプロジェクト）を、手で組み立てたバイト列で再現する。
// v1 の形式は変更してはならない。新しいバージョンでも、このデータを読み込めること。
namespace
{
std::vector<std::uint8_t> makeVersion1Blob()
{
    ByteWriter w;
    w.writeU32(kEditModelMagic);
    w.writeU32(1);
    w.writeU8(1);   // analysed
    w.writeU64(3);  // nextNoteId

    w.writeF64(0.0);
    w.writeF64(0.5);
    w.writeU64(1);
    w.writeF32(100.0f);
    w.writeF32(1.0f);

    w.writeU64(2);
    for (std::uint64_t id = 1; id <= 2; ++id)
    {
        w.writeU64(id);
        w.writeF64(static_cast<double>(id));        // start
        w.writeF64(static_cast<double>(id) + 0.5);  // end
        w.writeF32(60.0f + static_cast<float>(id));
        w.writeF32(0.25f);
        w.writeF32(id == 1 ? 150.0f : 0.0f); // pitchOffsetCents
        w.writeF32(0.5f);                     // centerCorrection
        w.writeF32(0.75f);                    // vibratoScale
        w.writeF32(1.0f);                     // driftScale
        w.writeF32(0.1f);                     // transitionSmoothness
        w.writeF64(-1.0);
        w.writeF64(-1.0);
    }

    return w.release();
}
} // namespace

TEST_CASE("バージョン 1 の保存データを読み込み、フェーズ2 の項目は既定値になる")
{
    const auto bytes = makeVersion1Blob();
    // ヘッダ 17 + トラック 32 + ノート数 8 + ノート 68 x 2
    REQUIRE(bytes.size() == 17 + 32 + 8 + 68 * 2);

    const auto restored = EditModel::deserialize(bytes.data(), bytes.size());
    REQUIRE(restored.has_value());
    REQUIRE(restored->notes().size() == 2);

    const auto& n = restored->notes()[0];
    CHECK(n.edit.pitchOffsetCents == 150.0f);
    CHECK(n.edit.centerCorrection == 0.5f);
    CHECK(n.edit.vibratoScale == 0.75f);
    CHECK(n.edit.gainDb == 0.0f);
    CHECK(n.edit.formantShiftCents == 0.0f);
    CHECK(n.edit.consonantGainDb == 0.0f);
    CHECK(restored->targets().empty());
    CHECK(restored->settings().renderMode == 0);
}

TEST_CASE("v1 から読み込んだモデルを保存し直すと、v2 として読める")
{
    const auto bytes = makeVersion1Blob();
    const auto restored = EditModel::deserialize(bytes.data(), bytes.size());
    REQUIRE(restored.has_value());

    const auto v2 = restored->serialize();
    CHECK(v2[4] == kEditModelFormatVersion);
    const auto again = EditModel::deserialize(v2.data(), v2.size());
    REQUIRE(again.has_value());
    CHECK(again->notes()[0].edit.pitchOffsetCents == 150.0f);
}

TEST_CASE("v3 の保存サイズ（固定）と、v2 データの読み込み")
{
    EditModel m;
    PitchTrack t;
    t.hopSec = 0.5;
    t.f0Hz = { 100.0f };
    t.voicing = { 1.0f };
    m.setAnalysis(t, { makeNote(1.0, 2.0, 60.0f) });

    // ヘッダ 17 + トラック 32 + ノート数 8 + ノート 88 + 目標数 8 + 割り当て数 8 + 設定 9 + 範囲 17
    auto bytes = m.serialize();
    CHECK(bytes.size() == 17 + 32 + 8 + 88 + 8 + 8 + 9 + 17);

    // 範囲の 17 バイトを除いてバージョンを 2 にすると、v2 のデータになる
    bytes.resize(bytes.size() - 17);
    bytes[4] = 2;
    const auto v2 = EditModel::deserialize(bytes.data(), bytes.size());
    REQUIRE(v2.has_value());
    CHECK_FALSE(v2->settings().timingRangeActive);
}

TEST_CASE("タイミング編集の範囲は保存・復元できる")
{
    auto m = makeModel();
    auto s = m.settings();
    s.timingRangeActive = true;
    s.timingRangeStartSec = 1.5;
    s.timingRangeEndSec = 4.25;
    m.setSettings(s);

    const auto bytes = m.serialize();
    const auto restored = EditModel::deserialize(bytes.data(), bytes.size());
    REQUIRE(restored.has_value());
    CHECK(restored->settings().timingRangeActive);
    CHECK(restored->settings().timingRangeStartSec == 1.5);
    CHECK(restored->settings().timingRangeEndSec == 4.25);
}

TEST_CASE("フェーズ2 の編集項目（ゲイン・フォルマント・子音・ブレス）は保存・復元できる")
{
    auto m = makeModel();
    auto edit = m.notes()[0].edit;
    edit.gainDb = -3.5f;
    edit.formantShiftCents = 200.0f;
    edit.formantFollow = 0.4f;
    edit.consonantGainDb = -6.0f;
    edit.breathGainDb = -12.0f;
    REQUIRE(m.setNoteEdit(m.notes()[0].id, edit));

    const auto bytes = m.serialize();
    const auto restored = EditModel::deserialize(bytes.data(), bytes.size());
    REQUIRE(restored.has_value());

    const auto& e = restored->notes()[0].edit;
    CHECK(e.gainDb == -3.5f);
    CHECK(e.formantShiftCents == 200.0f);
    CHECK(e.formantFollow == 0.4f);
    CHECK(e.consonantGainDb == -6.0f);
    CHECK(e.breathGainDb == -12.0f);
    CHECK_FALSE(e.isDefault());
}

TEST_CASE("目標ノートと割り当て、設定は保存・復元できる")
{
    auto m = makeModel();
    const auto a = m.notes()[0].id;
    const auto b = m.notes()[1].id;

    REQUIRE(m.setTargets({ { 1.0, 2.0, 62.0f }, { 2.5, 3.5, 65.0f } }, { { a, 0 }, { b, 1 } }));
    CHECK(m.assignmentOf(a) == 0);
    CHECK(m.assignmentOf(b) == 1);

    ModelSettings s;
    s.renderMode = 1;
    s.snapMode = 2;
    s.keyRoot = 7;
    s.keyMinor = 1;
    s.gridDivision = 3;
    s.manualBpm = 133.5f;
    m.setSettings(s);

    const auto bytes = m.serialize();
    const auto restored = EditModel::deserialize(bytes.data(), bytes.size());
    REQUIRE(restored.has_value());
    REQUIRE(restored->targets().size() == 2);
    CHECK(restored->targets()[1].midi == 65.0f);
    CHECK(restored->assignmentOf(b) == 1);
    CHECK(restored->settings().renderMode == 1);
    CHECK(restored->settings().keyRoot == 7);
    CHECK(restored->settings().keyMinor == 1);
    CHECK(restored->settings().gridDivision == 3);
    CHECK(restored->settings().manualBpm == 133.5f);
}

TEST_CASE("範囲外の設定値を持つ保存データは既定の設定に戻される")
{
    auto m = makeModel();
    auto bytes = m.serialize();
    // 設定の先頭（renderMode）は末尾から 9 バイト目
    bytes[bytes.size() - 9] = 7;
    const auto restored = EditModel::deserialize(bytes.data(), bytes.size());
    REQUIRE(restored.has_value());
    CHECK(restored->settings().renderMode == 0);
    CHECK(restored->settings().snapMode == 1);
}

TEST_CASE("目標の割り当ては入れ替え・解除でき、Undo で戻る")
{
    auto m = makeModel();
    const auto a = m.notes()[0].id;
    const auto b = m.notes()[1].id;
    REQUIRE(m.setTargets({ { 1.0, 2.0, 62.0f }, { 2.5, 3.5, 65.0f } }, { { a, 0 } }));

    REQUIRE(m.assignTarget(b, 0)); // 目標 0 は b へ移る（a は解除される）
    CHECK(m.assignmentOf(a) == -1);
    CHECK(m.assignmentOf(b) == 0);

    REQUIRE(m.assignTarget(b, -1));
    CHECK(m.assignmentOf(b) == -1);

    CHECK_FALSE(m.assignTarget(b, 5));  // 範囲外
    CHECK_FALSE(m.assignTarget(9999, 0)); // 存在しないノート

    REQUIRE(m.undo());
    CHECK(m.assignmentOf(b) == 0);
}

TEST_CASE("setTargets は不正な割り当て（範囲外・重複・存在しないノート）を捨てる")
{
    auto m = makeModel();
    const auto a = m.notes()[0].id;
    const auto b = m.notes()[1].id;

    REQUIRE(m.setTargets({ { 1.0, 2.0, 62.0f } }, { { a, 0 }, { b, 0 }, { 9999, 0 }, { b, 3 } }));
    CHECK(m.assignmentOf(a) == 0);
    CHECK(m.assignmentOf(b) == -1);
}

TEST_CASE("applyEdits は複数ノートを 1 回の Undo で変更し、重なるタイミングを押し出す")
{
    auto m = makeModel(); // 1.0-2.0, 2.5-3.5, 3.5-4.0
    const auto a = m.notes()[0].id;
    const auto b = m.notes()[1].id;

    auto ea = m.notes()[0].edit;
    ea.targetStartSec = 1.0;
    ea.targetEndSec = 3.0; // 次のノート（2.5 開始）に食い込む
    auto eb = m.notes()[1].edit;
    eb.pitchOffsetCents = 100.0f;

    REQUIRE(m.applyEdits({ { a, ea }, { b, eb } }));

    // b は a の終端（3.0）まで押し出される。長さは保たれ、後ろの 3 つ目も押し出される。
    CHECK(m.notes()[0].outEndSec() == 3.0);
    CHECK(m.notes()[1].outStartSec() == 3.0);
    CHECK(m.notes()[1].outEndSec() == 4.0);
    CHECK(m.notes()[2].outStartSec() >= 4.0);
    CHECK(m.notes()[1].edit.pitchOffsetCents == 100.0f);

    REQUIRE(m.undo());
    CHECK(m.notes()[0].outEndSec() == 2.0);
    CHECK(m.notes()[1].edit.pitchOffsetCents == 0.0f);
    CHECK_FALSE(m.canUndo());
}

TEST_CASE("設定の変更は revision を進め、Undo の対象にならない")
{
    auto m = makeModel();
    const auto r = m.revision();
    ModelSettings s = m.settings();
    s.renderMode = 1;
    m.setSettings(s);
    CHECK(m.revision() > r);
    CHECK_FALSE(m.canUndo());

    const auto r2 = m.revision();
    m.setSettings(s); // 同じ値
    CHECK(m.revision() == r2);
}

TEST_CASE("分割・結合では子音とブレスのゲインが正しい側に残る")
{
    auto m = makeModel();
    const auto id = m.notes()[0].id;
    auto e = m.notes()[0].edit;
    e.consonantGainDb = -4.0f;
    e.breathGainDb = -8.0f;
    m.setNoteEdit(id, e);

    NoteId right = 0;
    REQUIRE(m.splitNote(id, 1.5, &right));
    CHECK(m.findNote(id)->edit.consonantGainDb == -4.0f);
    CHECK(m.findNote(id)->edit.breathGainDb == 0.0f);
    CHECK(m.findNote(right)->edit.consonantGainDb == 0.0f);
    CHECK(m.findNote(right)->edit.breathGainDb == -8.0f);

    REQUIRE(m.mergeWithNext(id));
    CHECK(m.findNote(id)->edit.consonantGainDb == -4.0f);
    CHECK(m.findNote(id)->edit.breathGainDb == -8.0f);
}
