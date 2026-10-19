// MIDI 抽出（F-11）・MIDI をターゲットに適用（F-12）の変換。

#include "midi/SmfIO.h"
#include "model/MidiWorkflow.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace koenote;
using Catch::Matchers::WithinAbs;

namespace
{
model::Note note(model::NoteId id, double s, double e, float midi, float level = 0.1f)
{
    model::Note n;
    n.id = id;
    n.startSec = s;
    n.endSec = e;
    n.detectedMidi = midi;
    n.level = level;
    return n;
}

model::EditModel makeModel()
{
    model::EditModel m;
    model::PitchTrack t;
    t.hopSec = 0.01;
    t.f0Hz.assign(400, 0.0f);
    t.voicing.assign(400, 0.0f);
    // 0.5〜1.0 秒は 440 Hz（A4）より 50 セント高い
    for (std::size_t f = 50; f < 100; ++f)
    {
        t.f0Hz[f] = 440.0f * std::pow(2.0f, 50.0f / 1200.0f);
        t.voicing[f] = 1.0f;
    }
    m.setAnalysis(t, { note(0, 0.5, 1.0, 69.5f), note(0, 1.5, 2.0, 71.2f), note(0, 2.5, 3.0, 72.8f) });
    return m;
}
} // namespace

TEST_CASE("解析ノートの MIDI 化（元音源）は音高を四捨五入し、音量をベロシティにする")
{
    const auto m = makeModel();
    auto notes = m.notes();
    notes[0].level = 0.5f;   // 約 -6 dB → 最大
    notes[1].level = 0.001f; // -60 dB → 最小

    const auto out = model::notesToMidi(notes, false);
    REQUIRE(out.size() == 3);
    CHECK(out[0].pitch == 70); // 69.5 → 70
    CHECK(out[1].pitch == 71);
    CHECK(out[2].pitch == 73);
    CHECK(out[0].startSec == 0.5);
    CHECK(out[0].velocity >= 120);
    CHECK(out[1].velocity == 20);
}

TEST_CASE("MIDI 化（編集後）は目標音高と出力時間軸を使う")
{
    auto m = makeModel();
    m.setNotePitchOffset(m.notes()[0].id, 100.0f);
    m.setNoteTiming(m.notes()[0].id, 0.6, 1.2);

    const auto out = model::notesToMidi(m.notes(), true);
    CHECK(out[0].pitch == 71); // 69.5 + 1 = 70.5 → 71
    CHECK(out[0].startSec == 0.6);
    CHECK(out[0].endSec == 1.2);
}

TEST_CASE("ピッチベンド用の曲線はノート中心からの偏差になる")
{
    const auto m = makeModel();
    const auto curve = model::pitchBendCurve(m.pitchTrack(), m.notes());

    REQUIRE_FALSE(curve.empty());
    // 1 つ目のノート（中心 = round(69.5) = 70）に対し、f0 は 69.5 より... A4 + 50 セント = MIDI 69.5 → 偏差 -0.5
    CHECK_THAT(curve.front().semitones, WithinAbs(-0.5, 0.01));
    CHECK(curve.front().sec >= 0.5);
    CHECK(curve.back().sec < 1.0);
}

TEST_CASE("MIDI の書き出しと読み込みで、ノートの区間と音高が保たれる")
{
    const auto m = makeModel();
    const auto bytes = midi::writeSmf(model::notesToMidi(m.notes(), false));
    const auto seq = midi::readSmf(bytes.data(), bytes.size());

    REQUIRE(seq.has_value());
    REQUIRE(seq->notes.size() == 3);
    CHECK_THAT(seq->notes[1].startSec, WithinAbs(1.5, 0.002));
    CHECK_THAT(seq->notes[1].endSec, WithinAbs(2.0, 0.002));
    CHECK(seq->notes[1].pitch == 71);
}

TEST_CASE("MIDI の取り込み: 解析ノートと対応付けられ、未対応の数が数えられる")
{
    auto m = makeModel();

    midi::MidiSequence seq;
    seq.notes = { { 0.5, 1.0, 70, 80 }, { 1.5, 2.0, 72, 80 }, { 2.5, 3.0, 73, 80 }, { 4.0, 4.5, 60, 80 } };

    const auto imported = model::importMidi(m.notes(), seq, 0.0);
    CHECK(imported.targets.size() == 4);
    CHECK(imported.assignments.size() == 3);
    CHECK(imported.unmatchedAnalysis == 0);
    CHECK(imported.unmatchedTargets == 1);

    REQUIRE(m.setTargets(imported.targets, imported.assignments));
    CHECK(m.assignmentOf(m.notes()[1].id) == 1);
}

TEST_CASE("MIDI のオフセット: 先頭を合わせると、ずれた MIDI も対応付けられる")
{
    const auto m = makeModel();

    midi::MidiSequence seq;
    seq.notes = { { 10.5, 11.0, 70, 80 }, { 11.5, 12.0, 72, 80 }, { 12.5, 13.0, 73, 80 } };

    CHECK_THAT(model::autoMidiOffset(m.notes(), seq), WithinAbs(10.0, 1e-9));
    CHECK(model::importMidi(m.notes(), seq, 0.0).assignments.empty());
    CHECK(model::importMidi(m.notes(), seq, 10.0).assignments.size() == 3);
    CHECK(model::autoMidiOffset({}, seq) == 0.0);
}

TEST_CASE("目標を適用すると、ピッチとタイミングが強さの割合で変わる")
{
    auto m = makeModel();
    REQUIRE(m.setTargets({ { 0.6, 1.2, 72.0f }, { 1.5, 2.0, 71.0f } }, { { m.notes()[0].id, 0 }, { m.notes()[1].id, 1 } }));

    auto edits = model::computeTargetEdits(m, 1.0f, 1.0f);
    REQUIRE(edits.size() == 2); // 割り当てのない 3 つ目は含まれない
    CHECK_THAT(static_cast<double>(edits[0].second.pitchOffsetCents), WithinAbs((72.0 - 69.5) * 100.0, 1e-3));
    CHECK_THAT(edits[0].second.targetStartSec, WithinAbs(0.6, 1e-9));
    CHECK_THAT(edits[0].second.targetEndSec, WithinAbs(1.2, 1e-9));

    // 強さ 50%
    edits = model::computeTargetEdits(m, 0.5f, 0.5f);
    CHECK_THAT(static_cast<double>(edits[0].second.pitchOffsetCents), WithinAbs((72.0 - 69.5) * 50.0, 1e-3));
    CHECK_THAT(edits[0].second.targetStartSec, WithinAbs(0.55, 1e-9));

    // タイミング 0 なら変更しない
    edits = model::computeTargetEdits(m, 1.0f, 0.0f);
    CHECK_FALSE(edits[0].second.hasTimingEdit());

    REQUIRE(m.applyEdits(edits));
    CHECK_THAT(static_cast<double>(m.notes()[0].targetMidi()), WithinAbs(72.0, 1e-3));
}

TEST_CASE("手動の割り当てを反映して適用できる")
{
    auto m = makeModel();
    REQUIRE(m.setTargets({ { 0.5, 1.0, 60.0f }, { 1.5, 2.0, 62.0f } }, {}));
    REQUIRE(m.assignTarget(m.notes()[2].id, 1)); // 3 つ目のノートを目標 1 へ

    const auto edits = model::computeTargetEdits(m, 1.0f, 0.0f);
    REQUIRE(edits.size() == 1);
    CHECK(edits[0].first == m.notes()[2].id);
    CHECK_THAT(static_cast<double>(edits[0].second.pitchOffsetCents), WithinAbs((62.0 - 72.8) * 100.0, 1e-2));
}
