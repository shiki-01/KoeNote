// 解析 → 編集 → レンダリング → 再解析の一連の処理を、合成した歌声で検証する。

#include "analysis/RuleBasedNoteSegmenter.h"
#include "analysis/YinPitchAnalyzer.h"
#include "model/EditModel.h"
#include "render/PsolaRenderer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace koenote;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kSr = 44100.0;

// 2 つのノート（220 Hz と 261.63 Hz。いずれも弱いビブラート付き）を持つ声に近い合成音
std::vector<float> makeVoice(double total)
{
    const auto n = static_cast<std::size_t>(total * kSr);
    std::vector<float> x(n, 0.0f);
    double phase = 0.0;

    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / kSr;
        const bool first = t >= 0.3 && t < 1.3;
        const bool second = t >= 1.5 && t < 2.5;

        if (! first && ! second)
            continue;

        const double base = first ? 220.0 : 261.63;
        const double cents = 15.0 * std::sin(2.0 * std::numbers::pi * 5.5 * t);
        phase += 2.0 * std::numbers::pi * base * std::pow(2.0, cents / 1200.0) / kSr;

        double v = 0.0;
        for (int h = 1; h <= 8; ++h)
            v += std::sin(phase * h) / (h * 1.2);

        const double edge = std::min({ 1.0, (t - (first ? 0.3 : 1.5)) / 0.02, ((first ? 1.3 : 2.5) - t) / 0.03 });
        x[i] = static_cast<float>(0.3 * v * edge);
    }

    return x;
}

double medianF0(const model::PitchTrack& track, double fromSec, double toSec)
{
    std::vector<double> values;
    for (std::size_t f = 0; f < track.size(); ++f)
        if (track.timeOf(f) >= fromSec && track.timeOf(f) < toSec && track.isVoiced(f))
            values.push_back(track.f0Hz[f]);

    if (values.empty())
        return 0.0;

    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

double cents(double hz, double reference) { return 1200.0 * std::log2(hz / reference); }

struct Analysed
{
    std::vector<float> voice;
    model::EditModel model;
    analysis::YinPitchAnalyzer analyzer;
    model::PitchTrack track;

    Analysed()
    {
        voice = makeVoice(3.0);
        const analysis::MonoAudioView view { voice.data(), voice.size(), kSr };
        analysis::RuleBasedNoteSegmenter segmenter;
        track = analyzer.analyze(view, {});
        model.setAnalysis(track, segmenter.segment(track, view));
    }

    render::RenderedAudio render() const
    {
        const float* ptr = voice.data();
        render::RenderInput input;
        input.channels = &ptr;
        input.numChannels = 1;
        input.numFrames = static_cast<std::int64_t>(voice.size());
        input.sampleRate = kSr;
        input.track = &model.pitchTrack();
        input.notes = &model.notes();
        return render::renderEditedAudio(input);
    }

    model::PitchTrack analyse(const render::RenderedAudio& audio) const
    {
        const analysis::MonoAudioView view { audio.channels[0].data(), audio.channels[0].size(), kSr };
        return const_cast<analysis::YinPitchAnalyzer&>(analyzer).analyze(view, {});
    }
};
} // namespace

TEST_CASE("解析で 2 つのノートが得られ、ピッチと区間が正しい")
{
    Analysed a;

    REQUIRE(a.model.notes().size() == 2);
    CHECK_THAT(static_cast<double>(a.model.notes()[0].detectedMidi), WithinAbs(69.0 + 12.0 * std::log2(220.0 / 440.0), 0.2));
    CHECK_THAT(static_cast<double>(a.model.notes()[1].detectedMidi), WithinAbs(69.0 + 12.0 * std::log2(261.63 / 440.0), 0.2));
    CHECK_THAT(a.model.notes()[0].startSec, WithinAbs(0.3, 0.05));
    CHECK_THAT(a.model.notes()[1].endSec, WithinAbs(2.5, 0.05));
}

TEST_CASE("ピッチを編集してレンダリングし、再解析すると編集後のピッチになる")
{
    Analysed a;
    REQUIRE(a.model.notes().size() == 2);

    // 1 つ目を +300 セント、2 つ目を -200 セント
    a.model.setNotePitchOffset(a.model.notes()[0].id, 300.0f);
    a.model.setNotePitchOffset(a.model.notes()[1].id, -200.0f);

    const auto rendered = a.render();
    REQUIRE(rendered.numChannels == 1);

    const auto edited = a.analyse(rendered);
    const double firstExpected = 220.0 * std::pow(2.0, 300.0 / 1200.0);
    const double secondExpected = 261.63 * std::pow(2.0, -200.0 / 1200.0);

    CHECK_THAT(cents(medianF0(edited, 0.5, 1.2), firstExpected), WithinAbs(0.0, 30.0));
    CHECK_THAT(cents(medianF0(edited, 1.7, 2.4), secondExpected), WithinAbs(0.0, 30.0));
}

TEST_CASE("ビブラート除去でノート内のピッチの揺れが小さくなる")
{
    Analysed a;
    REQUIRE(a.model.notes().size() == 2);

    auto edit = a.model.notes()[0].edit;
    edit.vibratoScale = 0.0f;
    a.model.setNoteEdit(a.model.notes()[0].id, edit);

    const auto rendered = a.render();
    REQUIRE(rendered.numChannels == 1);
    const auto edited = a.analyse(rendered);

    const auto spread = [] (const model::PitchTrack& t)
    {
        double sum = 0.0, sumSq = 0.0;
        int count = 0;
        for (std::size_t f = 0; f < t.size(); ++f)
        {
            if (t.timeOf(f) < 0.5 || t.timeOf(f) > 1.2 || ! t.isVoiced(f))
                continue;
            const double c = 1200.0 * std::log2(static_cast<double>(t.f0Hz[f]) / 220.0);
            sum += c;
            sumSq += c * c;
            ++count;
        }
        const double mean = sum / count;
        return std::sqrt(std::max(0.0, sumSq / count - mean * mean));
    };

    // 元のビブラートは ±15 セント（標準偏差 約 10 セント）。除去後は半分以下になる。
    CHECK(spread(edited) < 0.5 * spread(a.track));
}

TEST_CASE("タイミングを遅らせると出力が長くなり、元の位置は無音になる")
{
    Analysed a;
    REQUIRE(a.model.notes().size() == 2);

    const auto second = a.model.notes()[1];
    REQUIRE(a.model.setNoteTiming(second.id, second.startSec + 0.4, second.endSec + 0.4));

    const auto rendered = a.render();
    REQUIRE(rendered.numChannels == 1);
    CHECK_THAT(static_cast<double>(rendered.numFrames()) / kSr, WithinAbs(3.4, 0.02));

    const auto rmsOf = [&] (double from, double to)
    {
        double sum = 0.0;
        const auto i0 = static_cast<std::size_t>(from * kSr), i1 = static_cast<std::size_t>(to * kSr);
        for (auto i = i0; i < i1; ++i)
            sum += static_cast<double>(rendered.channels[0][i]) * static_cast<double>(rendered.channels[0][i]);
        return std::sqrt(sum / static_cast<double>(i1 - i0));
    };

    // 2 つ目のノートは 0.4 秒遅れて始まる（1.9 秒付近）。元の 1.5〜1.8 秒付近は無音。
    CHECK(rmsOf(1.5, 1.8) < 0.01);
    CHECK(rmsOf(2.0, 2.8) > 0.05);
}

TEST_CASE("保存・復元したモデルは同じレンダリング結果になる")
{
    Analysed a;
    REQUIRE(a.model.notes().size() == 2);
    a.model.setNotePitchOffset(a.model.notes()[0].id, 150.0f);

    const auto bytes = a.model.serialize();
    auto restored = model::EditModel::deserialize(bytes.data(), bytes.size());
    REQUIRE(restored.has_value());

    const auto first = a.render();
    std::swap(a.model, *restored);
    const auto second = a.render();

    REQUIRE(first.numChannels == 1);
    CHECK(first.channels[0] == second.channels[0]);
}
