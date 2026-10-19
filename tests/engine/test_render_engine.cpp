// RenderEngine の結合テスト: worker thread での解析・レンダリング、結果の公開、取り消し、A/B 比較、ニューラル。
// ニューラルのテストは、環境変数 KOENOTE_NEURAL_DIR（vocoder.yaml と onnxruntime.dll があるディレクトリ）が
// 設定されている場合だけ実行する。

#include "analysis/DtwAligner.h"
#include "analysis/RuleBasedNoteSegmenter.h"
#include "analysis/YinPitchAnalyzer.h"
#include "plugin/RenderEngine.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <numbers>
#include <thread>

using namespace koenote;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double kSr = 44100.0;

class StubSource final : public plugin::ISourceAudio
{
public:
    explicit StubSource(std::vector<float> audio)
    {
        auto d = std::make_shared<plugin::SourceAudioData>();
        d->numChannels = 1;
        d->numFrames = static_cast<std::int64_t>(audio.size());
        d->sampleRate = kSr;
        d->channels = { std::move(audio) };
        d->channelPointers = { d->channels[0].data() };
        data = d;
    }

    std::shared_ptr<const plugin::SourceAudioData> sharedAudio() const override { return data; }
    const plugin::SourceAudioData* audioForPlayback() const noexcept override { return data.get(); }

    std::shared_ptr<plugin::SourceAudioData> data;
};

// 2 つのノート（220 Hz と 261.63 Hz）を持つ声に近い合成音
std::vector<float> makeVoice()
{
    const auto n = static_cast<std::size_t>(3.0 * kSr);
    std::vector<float> x(n, 0.0f);
    double phase = 0.0;

    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / kSr;
        const bool first = t >= 0.3 && t < 1.3;
        const bool second = t >= 1.6 && t < 2.6;

        if (! first && ! second)
            continue;

        phase += 2.0 * std::numbers::pi * (first ? 220.0 : 261.63) / kSr;
        double v = 0.0;
        for (int h = 1; h <= 10; ++h)
            v += std::sin(phase * h) / (h * 1.2);

        const double edge = std::min({ 1.0, (t - (first ? 0.3 : 1.6)) / 0.02, ((first ? 1.3 : 2.6) - t) / 0.03 });
        x[i] = static_cast<float>(0.3 * v * edge);
    }

    return x;
}

// 結果の通知を集める。takeEvents は取り出すと空になるため、累積して保持する。
struct Harness
{
    explicit Harness(std::vector<float> audio)
        : source(std::move(audio)), engine(gate, source, [this] { ++wakeCount; })
    {
    }

    bool waitFor(const std::function<bool()>& condition, int timeoutMs = 20000)
    {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

        while (std::chrono::steady_clock::now() < end)
        {
            collect();

            if (condition())
                return true;

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        collect();
        return condition();
    }

    void collect()
    {
        auto events = engine.takeEvents();
        if (events.analysis.has_value())
            analysis = std::move(events.analysis);
        analysisFailed = analysisFailed || events.analysisFailed;
        if (events.alignment != nullptr)
            alignment = std::move(events.alignment);
        if (events.renderFinished)
            ++renderFinishedCount;
    }

    plugin::RenderEngine::PlaybackViews views()
    {
        const auto scope = gate.tryRead();
        REQUIRE(scope.locked());
        return engine.viewsForPlayback();
    }

    render::ProcessingGate gate;
    StubSource source;
    plugin::RenderEngine engine;
    std::atomic<int> wakeCount { 0 };

    std::optional<plugin::RenderEngine::AnalysisResult> analysis;
    bool analysisFailed = false;
    std::unique_ptr<analysis::AlignmentResult> alignment;
    int renderFinishedCount = 0;
};

plugin::RenderEngine::RenderRequest makeRequest(const plugin::RenderEngine::AnalysisResult& a, std::uint64_t revision, float offsetCents,
                                                std::uint8_t renderMode = 0)
{
    plugin::RenderEngine::RenderRequest r;
    r.notes = a.notes;
    for (std::size_t i = 0; i < r.notes.size(); ++i)
    {
        r.notes[i].id = i + 1;
        r.notes[i].edit.pitchOffsetCents = offsetCents;
    }
    r.track = a.track;
    r.revision = revision;
    r.settings.renderMode = renderMode;
    return r;
}

double medianF0(const plugin::RenderEngine::PlaybackAudio& audio, double from, double to)
{
    std::vector<float> mono(audio.view.channels[0], audio.view.channels[0] + audio.view.numFrames);
    const analysis::MonoAudioView view { mono.data(), mono.size(), audio.sampleRate };
    analysis::YinPitchAnalyzer analyzer;
    const auto track = analyzer.analyze(view, {});

    std::vector<double> values;
    for (std::size_t f = 0; f < track.size(); ++f)
        if (track.timeOf(f) >= from && track.timeOf(f) < to && track.isVoiced(f))
            values.push_back(track.f0Hz[f]);

    std::sort(values.begin(), values.end());
    return values.empty() ? 0.0 : values[values.size() / 2];
}
} // namespace

TEST_CASE("解析は worker thread で行われ、ノートが得られる")
{
    Harness h(makeVoice());
    h.engine.requestAnalysis();

    REQUIRE(h.waitFor([&] { return h.analysis.has_value(); }));
    CHECK_FALSE(h.analysisFailed);
    CHECK(h.analysis->notes.size() == 2);
    CHECK(h.engine.analysisProgress() == 1.0f);
    CHECK(h.wakeCount.load() > 0);
}

TEST_CASE("無音の音声の解析は失敗として通知される")
{
    Harness h(std::vector<float>(static_cast<std::size_t>(kSr), 0.0f));
    h.engine.requestAnalysis();

    // 無音は有声区間がなく、ノートは空（失敗または空の結果）
    REQUIRE(h.waitFor([&] { return h.analysis.has_value() || h.analysisFailed; }));
    if (h.analysis.has_value())
        CHECK(h.analysis->notes.empty());
}

TEST_CASE("編集がなければ元音声がそのまま再生用に使われる")
{
    Harness h(makeVoice());
    h.engine.requestAnalysis();
    REQUIRE(h.waitFor([&] { return h.analysis.has_value(); }));

    // 編集前（レンダリング要求なし）
    auto before = h.views();
    CHECK(before.original.valid);
    CHECK(before.edited.valid);
    CHECK(before.edited.view.channels[0] == before.original.view.channels[0]);

    // 無編集のレンダリング要求
    h.engine.requestRender(makeRequest(*h.analysis, 1, 0.0f));
    REQUIRE(h.waitFor([&] { return h.renderFinishedCount > 0 && ! h.engine.isRenderPending(); }));

    const auto after = h.views();
    CHECK(after.edited.view.channels[0] == after.original.view.channels[0]);
}

TEST_CASE("ピッチを編集してレンダリングすると、再生用の音声が編集後に切り替わる")
{
    Harness h(makeVoice());
    h.engine.requestAnalysis();
    REQUIRE(h.waitFor([&] { return h.analysis.has_value(); }));

    h.engine.requestRender(makeRequest(*h.analysis, 2, 300.0f));
    REQUIRE(h.waitFor([&] { return h.renderFinishedCount > 0 && ! h.engine.isRenderPending(); }));

    const auto v = h.views();
    REQUIRE(v.edited.valid);
    CHECK(v.edited.view.channels[0] != v.original.view.channels[0]);

    const double expected = 220.0 * std::pow(2.0, 300.0 / 1200.0);
    CHECK_THAT(medianF0(v.edited, 0.5, 1.2), WithinAbs(expected, expected * 0.015));
}

TEST_CASE("続けて要求すると、最後の要求の結果が公開される")
{
    Harness h(makeVoice());
    h.engine.requestAnalysis();
    REQUIRE(h.waitFor([&] { return h.analysis.has_value(); }));

    h.engine.requestRender(makeRequest(*h.analysis, 3, 100.0f));
    h.engine.requestRender(makeRequest(*h.analysis, 4, 200.0f));
    h.engine.requestRender(makeRequest(*h.analysis, 5, 400.0f));
    REQUIRE(h.waitFor([&] { return ! h.engine.isRenderPending() && h.renderFinishedCount > 0; }));

    // 少し待っても結果が入れ替わらない（古い要求の結果が後から公開されない）
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    h.collect();

    const auto v = h.views();
    const double expected = 220.0 * std::pow(2.0, 400.0 / 1200.0);
    CHECK_THAT(medianF0(v.edited, 0.5, 1.2), WithinAbs(expected, expected * 0.015));
}

TEST_CASE("resetRendering は編集済みの音声を捨てて元音声へ戻す")
{
    Harness h(makeVoice());
    h.engine.requestAnalysis();
    REQUIRE(h.waitFor([&] { return h.analysis.has_value(); }));

    h.engine.requestRender(makeRequest(*h.analysis, 6, 300.0f));
    REQUIRE(h.waitFor([&] { return h.renderFinishedCount > 0 && ! h.engine.isRenderPending(); }));
    REQUIRE(h.views().edited.view.channels[0] != h.views().original.view.channels[0]);

    h.engine.resetRendering();
    h.collect();
    CHECK(h.views().edited.view.channels[0] == h.views().original.view.channels[0]);
    CHECK_FALSE(h.engine.isRenderPending());
}

TEST_CASE("A/B 比較の状態はオーディオスレッド側から読める")
{
    Harness h(makeVoice());

    plugin::CompareState s;
    s.useOriginal = true;
    s.rangeOnly = true;
    s.startSec = 0.5;
    s.endSec = 1.5;
    h.engine.setCompare(s);

    const auto v = h.views();
    CHECK(v.compare.useOriginal);
    CHECK(v.compare.rangeOnly);
    CHECK(v.compare.startSec == 0.5);
    CHECK(v.compare.endSec == 1.5);
    CHECK(h.engine.getCompare().endSec == 1.5);
}

TEST_CASE("ProcessingGate の writer が入場している間、reader は再生用の音声に触れない")
{
    Harness h(makeVoice());
    h.gate.beginWrite();
    {
        const auto scope = h.gate.tryRead();
        CHECK_FALSE(scope.locked());
    }
    h.gate.endWrite();
}

TEST_CASE("整列: 同じ音声へ整列すると、ノートのタイミングはほぼ変わらない")
{
    Harness h(makeVoice());
    h.engine.requestAnalysis();
    REQUIRE(h.waitFor([&] { return h.analysis.has_value(); }));

    h.engine.requestAlign(h.source.data);
    REQUIRE(h.waitFor([&] { return h.alignment != nullptr; }));
    REQUIRE(h.alignment->valid);

    for (const auto& n : h.analysis->notes)
        CHECK_THAT(h.alignment->mapTargetToGuide(n.startSec), WithinAbs(n.startSec, 0.03));
}

TEST_CASE("実モデル: ニューラル加工の結果が、TD-PSOLA の結果の後に再生用へ切り替わる")
{
    const char* dir = std::getenv("KOENOTE_NEURAL_DIR");
    if (dir == nullptr)
    {
        SUCCEED("KOENOTE_NEURAL_DIR が未設定のため省略した");
        return;
    }

    Harness h(makeVoice());
    h.engine.setNeuralDebounceMs(100);
    h.engine.requestAnalysis();
    REQUIRE(h.waitFor([&] { return h.analysis.has_value(); }));

    h.engine.requestRender(makeRequest(*h.analysis, 7, 300.0f, 1));

    // まず TD-PSOLA の結果が公開される（音切れなし）
    REQUIRE(h.waitFor([&] { return h.renderFinishedCount > 0 && ! h.engine.isRenderPending(); }));
    const auto psola = h.views();
    REQUIRE(psola.edited.valid);

    // ニューラルが完了すると、再生用の音声が切り替わる。完了までの間も、常に有効な音声が得られる。
    const auto start = std::chrono::steady_clock::now();
    bool switched = false;

    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(60) && ! switched)
    {
        h.collect();
        const auto v = h.views();
        REQUIRE(v.edited.valid);
        switched = v.edited.view.channels[0] != psola.edited.view.channels[0];
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const auto info = h.engine.neuralInfo();
    INFO(info.message.toStdString());
    REQUIRE(switched);
    CHECK(info.state == plugin::NeuralInfo::State::Ready);

    const double expected = 220.0 * std::pow(2.0, 300.0 / 1200.0);
    CHECK_THAT(medianF0(h.views().edited, 0.6, 1.2), WithinAbs(expected, expected * 0.015));
}

TEST_CASE("実モデル: 編集が続いている間はニューラル加工が取り消され、最新の編集に対して合成される")
{
    const char* dir = std::getenv("KOENOTE_NEURAL_DIR");
    if (dir == nullptr)
    {
        SUCCEED("KOENOTE_NEURAL_DIR が未設定のため省略した");
        return;
    }

    Harness h(makeVoice());
    h.engine.setNeuralDebounceMs(100);
    h.engine.requestAnalysis();
    REQUIRE(h.waitFor([&] { return h.analysis.has_value(); }));

    h.engine.requestRender(makeRequest(*h.analysis, 8, 100.0f, 1));
    REQUIRE(h.waitFor([&] { return h.renderFinishedCount > 0 && ! h.engine.isRenderPending(); }));

    // ニューラルの合成が始まった頃に、次の編集を行う
    REQUIRE(h.waitFor([&] { return h.engine.neuralInfo().state == plugin::NeuralInfo::State::Rendering; }, 10000));
    h.engine.requestRender(makeRequest(*h.analysis, 9, 400.0f, 1));

    // 最終的に、最新の編集（+400 セント）のニューラルの結果が使われる
    REQUIRE(h.waitFor([&] { return h.engine.neuralInfo().state == plugin::NeuralInfo::State::Ready && ! h.engine.isRenderPending(); }, 60000));

    const double expected = 220.0 * std::pow(2.0, 400.0 / 1200.0);
    CHECK_THAT(medianF0(h.views().edited, 0.6, 1.2), WithinAbs(expected, expected * 0.015));
}
