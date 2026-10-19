// MIX 向けの機能（F-22〜F-29）: 手描きのピッチ曲線、頭・尻の除外、歯擦音、音量の均し、ゲインの一括設定、
// 編集の貼り付け、ずれの表示、ガイドへの追従。

#include "model/MixCorrection.h"
#include "model/PitchDrawing.h"
#include "render/GainEnvelope.h"
#include "render/PitchCurveEditor.h"
#include "render/PsolaRenderer.h"
#include "render/Sibilance.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace koenote;
using Catch::Matchers::WithinAbs;

namespace
{
model::Note makeNote(model::NoteId id, double start, double end, float midi, float level = 0.1f)
{
    model::Note n;
    n.id = id;
    n.startSec = start;
    n.endSec = end;
    n.detectedMidi = midi;
    n.level = level;
    return n;
}

constexpr double kSr = 44100.0;

// 0〜0.25 秒: 高域のノイズ（「す」の子音）、0.25〜1.25 秒: 220 Hz の有声音、1.25〜1.5 秒: 低域のノイズ（ブレス）
struct Voice
{
    std::vector<float> audio;
    model::PitchTrack track;
    std::vector<model::Note> notes;

    Voice()
    {
        const auto n = static_cast<std::size_t>(1.6 * kSr);
        audio.assign(n, 0.0f);
        std::uint32_t seed = 7;
        const auto noise = [&seed] { seed = seed * 1664525u + 1013904223u; return static_cast<float>(seed >> 8) / 8388608.0f - 1.0f; };

        float previous = 0.0f;
        float lowState = 0.0f;

        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double>(i) / kSr;
            const float w = noise();

            if (t < 0.25)
            {
                // 1 次差分で高域を強調したノイズ
                audio[i] = 0.2f * (w - previous);
            }
            else if (t < 1.25)
            {
                double v = 0.0;
                for (int h = 1; h <= 20; ++h)
                    v += std::sin(2.0 * std::numbers::pi * 220.0 * h * t) / h;
                audio[i] = static_cast<float>(0.2 * v);
            }
            else if (t < 1.5)
            {
                // 1 次の低域通過（約 300 Hz）のノイズ
                lowState += 0.04f * (w - lowState);
                audio[i] = 0.5f * lowState;
            }

            previous = w;
        }

        track.hopSec = 0.005;
        const auto frames = static_cast<std::size_t>(1.6 / track.hopSec) + 1;
        track.f0Hz.assign(frames, 0.0f);
        track.voicing.assign(frames, 0.0f);

        for (std::size_t f = 0; f < frames; ++f)
        {
            const double t = track.timeOf(f);
            if (t >= 0.26 && t < 1.24)
            {
                track.f0Hz[f] = 220.0f;
                track.voicing[f] = 1.0f;
            }
        }

        notes = { makeNote(1, 0.26, 1.24, 57.0f) };
    }

    // [from, to) の RMS
    static double rms(const std::vector<float>& x, double from, double to)
    {
        const auto i0 = static_cast<std::size_t>(from * kSr);
        const auto i1 = std::min(x.size(), static_cast<std::size_t>(to * kSr));
        double sum = 0.0;
        for (std::size_t i = i0; i < i1; ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, i1 - i0)));
    }
};

render::RenderedAudio renderVoice(const Voice& v, const model::PitchDrawing* drawing = nullptr)
{
    const float* channels[1] = { v.audio.data() };
    render::RenderInput input;
    input.channels = channels;
    input.numChannels = 1;
    input.numFrames = static_cast<std::int64_t>(v.audio.size());
    input.sampleRate = kSr;
    input.track = &v.track;
    input.notes = &v.notes;
    input.drawing = drawing;
    return render::renderEditedAudio(input);
}
} // namespace

//==============================================================================
// PitchDrawing
TEST_CASE("手描き: 重なる・隣接する区間は 1 つにまとまり、消去で分かれる")
{
    model::PitchDrawing d;
    d.draw(10, { 60.0f, 60.0f, 60.0f });
    d.draw(13, { 61.0f, 61.0f });       // 隣接
    d.draw(20, { 62.0f });
    REQUIRE(d.segments().size() == 2);
    CHECK(d.segments()[0].startFrame == 10);
    CHECK(d.segments()[0].midi.size() == 5);

    d.draw(11, { 59.0f });              // 上書き
    float midi = 0.0f;
    REQUIRE(d.midiAt(11, midi));
    CHECK(midi == 59.0f);
    CHECK_FALSE(d.midiAt(15, midi));

    d.erase(12, 14);
    REQUIRE(d.segments().size() == 3);
    CHECK(d.segments()[0].midi.size() == 2);
    CHECK(d.segments()[1].startFrame == 14);
    CHECK(d.totalFrames() == 4);

    d.draw(9, std::vector<float>(13, 64.0f)); // 全体を覆う
    REQUIRE(d.segments().size() == 1);
    CHECK(d.segments()[0].startFrame == 9);
    CHECK(d.segments()[0].midi.size() == 13);
}

TEST_CASE("手描き: 不正な区間（重なり・空・範囲外の値）は fromSegments で拒否される")
{
    model::PitchDrawing d;
    CHECK_FALSE(model::PitchDrawing::fromSegments({ { 0, { 60.0f } }, { 1, { 60.0f } } }, d)); // 隣接
    CHECK_FALSE(model::PitchDrawing::fromSegments({ { 0, {} } }, d));
    CHECK_FALSE(model::PitchDrawing::fromSegments({ { 0, { -1.0f } } }, d));
    CHECK(model::PitchDrawing::fromSegments({ { 0, { 60.0f } }, { 5, { 61.0f } } }, d));
}

//==============================================================================
// ピッチ曲線（F-22・F-23）
TEST_CASE("手描きの区間では、描いた音高になる補正量が使われる（端は滑らかにつなぐ）")
{
    model::PitchTrack track;
    track.hopSec = 0.01;
    track.f0Hz.assign(100, 220.0f); // A3 = 57
    track.voicing.assign(100, 1.0f);
    const std::vector<model::Note> notes { makeNote(1, 0.0, 1.0, 57.0f) };

    model::PitchDrawing d;
    d.draw(40, std::vector<float>(20, 58.0f)); // 1 半音上

    const auto adjust = render::computePitchAdjustCents(track, notes, &d);
    CHECK_THAT(adjust[50], WithinAbs(100.0, 1e-3));
    CHECK_THAT(adjust[30], WithinAbs(0.0, 1e-3));
    // 端は 0 と 100 の間
    CHECK(adjust[40] > 0.0f);
    CHECK(adjust[40] < 100.0f);
}

TEST_CASE("頭・尻の除外: ノートの頭ではピッチ中心の補正が弱まり、しゃくりが残る")
{
    model::PitchTrack track;
    track.hopSec = 0.005;
    const std::size_t frames = 200; // 1 秒
    track.f0Hz.resize(frames);
    track.voicing.assign(frames, 1.0f);

    // 最初の 0.15 秒は 1 半音下からしゃくり上げる
    for (std::size_t f = 0; f < frames; ++f)
    {
        const double t = track.timeOf(f);
        const double cents = t < 0.15 ? -100.0 * (1.0 - t / 0.15) : 0.0;
        track.f0Hz[f] = static_cast<float>(220.0 * std::pow(2.0, cents / 1200.0));
    }

    auto note = makeNote(1, 0.0, 1.0, 57.0f);
    note.edit.centerCorrection = 1.0f;

    const auto full = render::computePitchAdjustCents(track, { note });
    note.edit.attackHoldSec = 0.2f;
    const auto held = render::computePitchAdjustCents(track, { note });

    // 先頭付近: 完全な補正ではしゃくりを持ち上げる（+）が、除外ありでは補正がほぼない
    CHECK(full[2] > 30.0f);
    CHECK(std::abs(held[2]) < 5.0f);
    // ノートの中央は同じ
    CHECK_THAT(held[100], WithinAbs(full[100], 1.0));
}

//==============================================================================
// 歯擦音（F-24）
TEST_CASE("歯擦音: 高域のノイズは検出され、有声音と低域のノイズは検出されない")
{
    const Voice v;
    const auto amount = render::detectSibilance(v.audio.data(), v.audio.size(), kSr, v.track);
    REQUIRE(amount.size() == v.track.size());

    const auto at = [&] (double sec) { return amount[static_cast<std::size_t>(std::llround(sec / v.track.hopSec))]; };

    CHECK(at(0.1) > 0.8f);
    CHECK(at(0.7) == 0.0f);
    CHECK(at(1.4) < 0.2f);
}

TEST_CASE("歯擦音の所属: 直後のノートの手前 0.3 秒以内は直後のノート、ノートの中はそのノート、それ以外は直前のノート")
{
    const std::vector<model::Note> notes { makeNote(1, 1.0, 2.0, 60.0f), makeNote(2, 3.0, 4.0, 62.0f) };
    CHECK(render::sibilanceOwner(notes, 0.2) == 0);
    CHECK(render::sibilanceOwner(notes, 1.5) == 0);
    CHECK(render::sibilanceOwner(notes, 2.2) == 0);
    CHECK(render::sibilanceOwner(notes, 2.8) == 1);
    CHECK(render::sibilanceOwner(notes, 4.5) == 1);
    CHECK(render::sibilanceOwner({}, 1.0) == -1);
}

TEST_CASE("歯擦音のゲインを下げると、子音のノイズだけが小さくなる")
{
    Voice v;
    const auto before = renderVoice(v);

    v.notes[0].edit.sibilanceGainDb = -12.0f;
    const auto after = renderVoice(v);
    REQUIRE(after.numChannels == 1);

    const double sBefore = Voice::rms(before.channels[0], 0.05, 0.2);
    const double sAfter = Voice::rms(after.channels[0], 0.05, 0.2);
    CHECK(sAfter < sBefore * 0.35); // -12 dB ≈ 0.25 倍

    // 有声音とブレスはほぼ変わらない
    CHECK_THAT(Voice::rms(after.channels[0], 0.4, 1.1), WithinAbs(Voice::rms(before.channels[0], 0.4, 1.1), 0.01));
    CHECK_THAT(Voice::rms(after.channels[0], 1.3, 1.45), WithinAbs(Voice::rms(before.channels[0], 1.3, 1.45), 0.01));
}

TEST_CASE("歯擦音のゲインだけでも、編集ありとしてレンダリングされる")
{
    std::vector<model::Note> notes { makeNote(1, 0.0, 1.0, 60.0f) };
    CHECK_FALSE(render::hasAudibleEdits(notes));
    notes[0].edit.sibilanceGainDb = -3.0f;
    CHECK(render::hasAudibleEdits(notes));

    model::PitchDrawing d;
    notes[0].edit.sibilanceGainDb = 0.0f;
    CHECK_FALSE(render::hasAudibleEdits(notes, &d));
    d.draw(0, { 60.0f });
    CHECK(render::hasAudibleEdits(notes, &d));
}

TEST_CASE("手描きで 1 半音上げると、レンダリング結果のピッチが上がる")
{
    const Voice v;
    model::PitchDrawing d;
    // 0.5〜0.9 秒を A#3（58）に
    const auto first = static_cast<std::uint32_t>(0.5 / v.track.hopSec);
    d.draw(first, std::vector<float>(80, 58.0f));

    const auto out = renderVoice(v, &d);
    REQUIRE(out.numChannels == 1);

    // 0.6〜0.8 秒のゼロ交差の数から基本周波数を見積もる（倍音は 1/h で減衰するため、基本周波数が支配的）
    const auto& x = out.channels[0];
    const auto i0 = static_cast<std::size_t>(0.6 * kSr);
    const auto i1 = static_cast<std::size_t>(0.8 * kSr);
    int crossings = 0;
    for (std::size_t i = i0 + 1; i < i1; ++i)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f))
            ++crossings;

    const double f0 = crossings / 2.0 / 0.2;
    const double expected = 220.0 * std::pow(2.0, 1.0 / 12.0);
    CHECK_THAT(f0, WithinAbs(expected, expected * 0.05));
}

//==============================================================================
// 一括操作（F-25〜F-29）
TEST_CASE("音量の均し: 長さで重み付けした平均の音量へ、強さの割合だけ寄せる")
{
    const std::vector<model::Note> notes {
        makeNote(1, 0.0, 1.0, 60.0f, 0.1f),  // -20 dB
        makeNote(2, 1.0, 2.0, 60.0f, 0.01f), // -40 dB
        makeNote(3, 2.0, 3.0, 60.0f, 0.0f),  // 音量 0 は対象外
    };

    const auto edits = model::computeLevelingEdits(notes, {}, 1.0f, 30.0f);
    REQUIRE(edits.size() == 2);
    CHECK_THAT(edits[0].second.gainDb, WithinAbs(-10.0, 1e-3));
    CHECK_THAT(edits[1].second.gainDb, WithinAbs(10.0, 1e-3));

    const auto half = model::computeLevelingEdits(notes, {}, 0.5f, 30.0f);
    CHECK_THAT(half[0].second.gainDb, WithinAbs(-5.0, 1e-3));

    // 変更量の制限
    const auto limited = model::computeLevelingEdits(notes, {}, 1.0f, 6.0f);
    CHECK_THAT(limited[1].second.gainDb, WithinAbs(6.0, 1e-3));

    // 選択ノートだけ（1 つだけなら平均と同じで変化なし）
    const auto one = model::computeLevelingEdits(notes, { 2 }, 1.0f);
    REQUIRE(one.size() == 1);
    CHECK_THAT(one[0].second.gainDb, WithinAbs(0.0, 1e-3));
}

TEST_CASE("ゲインの一括設定は、指定した項目だけを変える")
{
    auto a = makeNote(1, 0.0, 1.0, 60.0f);
    a.edit.gainDb = 2.0f;
    const std::vector<model::Note> notes { a, makeNote(2, 1.0, 2.0, 62.0f) };

    const auto breath = model::computeGainEdits(notes, {}, model::GainTarget::Breath, -9.0f);
    REQUIRE(breath.size() == 2);
    CHECK(breath[0].second.breathGainDb == -9.0f);
    CHECK(breath[0].second.gainDb == 2.0f);

    const auto sib = model::computeGainEdits(notes, { 2 }, model::GainTarget::Sibilance, -6.0f);
    REQUIRE(sib.size() == 1);
    CHECK(sib[0].first == 2);
    CHECK(sib[0].second.sibilanceGainDb == -6.0f);
}

TEST_CASE("編集の貼り付け: ピッチ移動量とタイミングは写さない")
{
    model::NoteEdit src;
    src.pitchOffsetCents = 300.0f;
    src.centerCorrection = 0.7f;
    src.vibratoScale = 0.5f;
    src.attackHoldSec = 0.1f;
    src.formantShiftCents = 50.0f;
    src.sibilanceGainDb = -4.0f;
    src.targetStartSec = 5.0;
    src.targetEndSec = 6.0;

    model::NoteEdit dst;
    dst.pitchOffsetCents = -100.0f;

    const auto result = model::pasteEditParameters(dst, src);
    CHECK(result.pitchOffsetCents == -100.0f);
    CHECK_FALSE(result.hasTimingEdit());
    CHECK(result.centerCorrection == 0.7f);
    CHECK(result.vibratoScale == 0.5f);
    CHECK(result.attackHoldSec == 0.1f);
    CHECK(result.formantShiftCents == 50.0f);
    CHECK(result.sibilanceGainDb == -4.0f);
}

TEST_CASE("ずれの表示: 編集後のピッチ中心とスナップ先の差（セント）")
{
    auto n = makeNote(1, 0.0, 1.0, 60.3f);
    CHECK_THAT(model::pitchDeviationCents(n, model::SnapMode::Semitone, 0, model::ScaleType::Major), WithinAbs(30.0, 1e-3));
    // Off は半音として扱う
    CHECK_THAT(model::pitchDeviationCents(n, model::SnapMode::Off, 0, model::ScaleType::Major), WithinAbs(30.0, 1e-3));

    n.edit.pitchOffsetCents = -30.0f;
    CHECK_THAT(model::pitchDeviationCents(n, model::SnapMode::Semitone, 0, model::ScaleType::Major), WithinAbs(0.0, 1e-3));

    // スケール: C メジャーで 61.2（C#）は D（62）へのずれ
    const auto s = makeNote(2, 0.0, 1.0, 61.2f);
    CHECK_THAT(model::pitchDeviationCents(s, model::SnapMode::Scale, 0, model::ScaleType::Major), WithinAbs(-80.0, 1e-3));
}

TEST_CASE("ガイドへの追従: ユニゾンはガイドの音高へ、音程を保つ場合は最も近い半音の音程差を保つ")
{
    // ガイドは 0.1 秒遅れて始まる（offset = +0.1）
    const std::vector<model::Note> guide { makeNote(1, 0.1, 1.1, 60.2f), makeNote(2, 1.2, 2.1, 62.0f) };
    const std::vector<model::Note> notes {
        makeNote(10, 0.0, 1.0, 59.9f),  // ガイドの 1 番目と重なる（ダブル）
        makeNote(11, 1.1, 2.0, 65.3f),  // ガイドの 2 番目と重なる（3 度上のハモリ）
        makeNote(12, 5.0, 6.0, 60.0f),  // 対応なし
    };

    model::GuideFollowParams p;
    p.offsetSec = 0.1;
    int matched = 0;

    auto edits = model::computeGuidePitchEdits(notes, {}, guide, p, &matched);
    CHECK(matched == 2);
    REQUIRE(edits.size() == 2);
    CHECK_THAT(edits[0].second.pitchOffsetCents, WithinAbs(30.0, 1e-3));   // 59.9 → 60.2
    CHECK_THAT(edits[1].second.pitchOffsetCents, WithinAbs(-330.0, 1e-3)); // 65.3 → 62.0

    p.mode = model::GuideFollowParams::Mode::KeepInterval;
    edits = model::computeGuidePitchEdits(notes, {}, guide, p);
    REQUIRE(edits.size() == 2);
    CHECK_THAT(edits[0].second.pitchOffsetCents, WithinAbs(30.0, 1e-3)); // 音程差 0
    CHECK_THAT(edits[1].second.pitchOffsetCents, WithinAbs(-30.0, 1e-3)); // 62 + 3 = 65.0

    p.strength = 0.5f;
    edits = model::computeGuidePitchEdits(notes, { 11 }, guide, p);
    REQUIRE(edits.size() == 1);
    CHECK_THAT(edits[0].second.pitchOffsetCents, WithinAbs(-15.0, 1e-3));
}
