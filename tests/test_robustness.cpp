// 異常な入力（極端に短い音声、範囲外のノート、巨大な時間移動など）でクラッシュ・巨大確保をしないことを確認する。

#include "analysis/RuleBasedNoteSegmenter.h"
#include "analysis/YinPitchAnalyzer.h"
#include "model/EditModel.h"
#include "render/PsolaRenderer.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

using namespace koenote;

namespace
{
std::vector<float> sine(std::size_t n, double hz, double sr)
{
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / sr));
    return x;
}
} // namespace

TEST_CASE("極端に短い音声でも解析はクラッシュしない")
{
    analysis::YinPitchAnalyzer analyzer;
    analysis::RuleBasedNoteSegmenter segmenter;

    for (const std::size_t n : { std::size_t { 1 }, std::size_t { 2 }, std::size_t { 10 }, std::size_t { 100 }, std::size_t { 1000 }, std::size_t { 5000 } })
    {
        const auto x = sine(n, 220.0, 44100.0);
        const analysis::MonoAudioView view { x.data(), x.size(), 44100.0 };
        const auto track = analyzer.analyze(view, {});
        const auto notes = segmenter.segment(track, view);
        CHECK(notes.size() <= 1);
    }
}

TEST_CASE("サンプルレートが極端な音声でもクラッシュしない")
{
    analysis::YinPitchAnalyzer analyzer;

    for (const double sr : { 8000.0, 22050.0, 96000.0, 192000.0 })
    {
        const auto x = sine(static_cast<std::size_t>(sr * 0.5), 220.0, sr);
        const analysis::MonoAudioView view { x.data(), x.size(), sr };
        const auto track = analyzer.analyze(view, {});
        CHECK_FALSE(track.empty());
    }
}

TEST_CASE("全サンプルが同じ値、または NaN を含む音声でもクラッシュしない")
{
    analysis::YinPitchAnalyzer analyzer;

    std::vector<float> dc(44100, 0.5f);
    CHECK_NOTHROW(analyzer.analyze({ dc.data(), dc.size(), 44100.0 }, {}));

    auto withNan = sine(44100, 220.0, 44100.0);
    withNan[1000] = std::numeric_limits<float>::quiet_NaN();
    CHECK_NOTHROW(analyzer.analyze({ withNan.data(), withNan.size(), 44100.0 }, {}));
}

TEST_CASE("音声の範囲外にあるノートや空のピッチ曲線でもレンダリングは完了する")
{
    const auto x = sine(44100, 220.0, 44100.0);
    const float* ptr = x.data();

    model::PitchTrack empty;
    std::vector<model::Note> notes(1);
    notes[0].startSec = 5.0; // 音声（1 秒）より後ろ
    notes[0].endSec = 6.0;
    notes[0].edit.pitchOffsetCents = 100.0f;

    render::RenderInput input;
    input.channels = &ptr;
    input.numChannels = 1;
    input.numFrames = static_cast<std::int64_t>(x.size());
    input.sampleRate = 44100.0;
    input.track = &empty;
    input.notes = &notes;

    const auto out = render::renderEditedAudio(input);
    CHECK(out.numChannels == 1);
}

TEST_CASE("巨大な時間移動でも出力長が制限される")
{
    const auto x = sine(44100, 220.0, 44100.0);
    const float* ptr = x.data();

    model::PitchTrack track;
    track.hopSec = 0.005;
    track.f0Hz.assign(201, 220.0f);
    track.voicing.assign(201, 1.0f);

    std::vector<model::Note> notes(1);
    notes[0].startSec = 0.1;
    notes[0].endSec = 0.9;
    notes[0].detectedMidi = 57.0f;
    notes[0].edit.targetStartSec = 100000.0;
    notes[0].edit.targetEndSec = 100001.0;

    render::RenderInput input;
    input.channels = &ptr;
    input.numChannels = 1;
    input.numFrames = static_cast<std::int64_t>(x.size());
    input.sampleRate = 44100.0;
    input.track = &track;
    input.notes = &notes;

    const auto out = render::renderEditedAudio(input);
    REQUIRE(out.numChannels == 1);
    CHECK(out.numFrames() <= static_cast<std::int64_t>(x.size()) * 2 + 30 * 44100 + 1);
}

TEST_CASE("極端に大きなピッチ移動でも出力は有限の値になる")
{
    const auto x = sine(44100, 220.0, 44100.0);
    const float* ptr = x.data();

    model::PitchTrack track;
    track.hopSec = 0.005;
    track.f0Hz.assign(201, 220.0f);
    track.voicing.assign(201, 1.0f);

    std::vector<model::Note> notes(1);
    notes[0].startSec = 0.1;
    notes[0].endSec = 0.9;
    notes[0].detectedMidi = 57.0f;

    for (const float cents : { 2400.0f, -2400.0f, 10000.0f, -10000.0f })
    {
        notes[0].edit.pitchOffsetCents = cents;

        render::RenderInput input;
        input.channels = &ptr;
        input.numChannels = 1;
        input.numFrames = static_cast<std::int64_t>(x.size());
        input.sampleRate = 44100.0;
        input.track = &track;
        input.notes = &notes;

        const auto out = render::renderEditedAudio(input);
        REQUIRE(out.numChannels == 1);

        bool allFinite = true;
        for (const auto v : out.channels[0])
            allFinite = allFinite && std::isfinite(v);
        CHECK(allFinite);
    }
}

TEST_CASE("壊れた編集モデルは検証で拒否される（NaN・逆転した区間）")
{
    model::EditModel m;
    model::Note a;
    a.startSec = 1.0;
    a.endSec = 2.0;
    model::PitchTrack t;
    t.f0Hz = { 0.0f };
    t.voicing = { 0.0f };
    m.setAnalysis(t, { a });

    const auto id = m.notes()[0].id;
    CHECK_FALSE(m.setNotePitchOffset(id, std::numeric_limits<float>::quiet_NaN()));
    CHECK_FALSE(m.setNoteTiming(id, 3.0, 2.0));
    CHECK_FALSE(m.moveBoundary(id, std::numeric_limits<double>::quiet_NaN()));
}
