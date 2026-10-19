#include "analysis/KeyDetector.h"
#include "analysis/MidiMatcher.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <vector>

using namespace koenote::analysis;
using Catch::Matchers::WithinAbs;

namespace
{
NoteForKey nk(float midi, double dur)
{
    return NoteForKey{ midi, dur };
}

TimedNote tn(double start, double end, float midi)
{
    return TimedNote{ start, end, midi };
}
} // namespace

//==============================================================================
// KeyDetector

TEST_CASE("Cメジャーのスケールを C メジャーとして検出する")
{
    // C D E F G A B C（各 0.5 秒）
    const std::vector<NoteForKey> notes = {
        nk(60, 0.5), nk(62, 0.5), nk(64, 0.5), nk(65, 0.5),
        nk(67, 0.5), nk(69, 0.5), nk(71, 0.5), nk(72, 0.5),
    };
    const auto r = detectKey(notes);

    REQUIRE(r.valid);
    CHECK(r.root == 0);
    CHECK_FALSE(r.minor);
    CHECK(r.confidence >= 0.0f);
    CHECK(r.confidence <= 1.0f);
}

TEST_CASE("Aマイナーのメロディを A マイナーとして検出する")
{
    // A B C D E F G A。主音 A と E を長めにする
    const std::vector<NoteForKey> notes = {
        nk(69, 1.0), nk(71, 0.5), nk(60, 0.5), nk(62, 0.5),
        nk(64, 1.0), nk(65, 0.5), nk(67, 0.5), nk(69, 1.0),
    };
    const auto r = detectKey(notes);

    REQUIRE(r.valid);
    CHECK(r.root == 9);
    CHECK(r.minor);
}

TEST_CASE("Gメジャー（F# を含む）を G メジャーとして検出する")
{
    // G A B C D E F# G
    const std::vector<NoteForKey> notes = {
        nk(67, 0.5), nk(69, 0.5), nk(71, 0.5), nk(72, 0.5),
        nk(74, 0.5), nk(76, 0.5), nk(78, 0.5), nk(79, 0.5),
    };
    const auto r = detectKey(notes);

    REQUIRE(r.valid);
    CHECK(r.root == 7);
    CHECK_FALSE(r.minor);
}

TEST_CASE("空入力と 1 音のみは valid が false になる")
{
    const auto empty = detectKey({});
    CHECK_FALSE(empty.valid);
    CHECK(empty.ranking.empty());

    const auto single = detectKey({ nk(60, 1.0) });
    CHECK_FALSE(single.valid);
    CHECK(single.ranking.empty());

    // 同一音高の繰り返しも 1 ピッチクラスのため無効
    const auto sameKey = detectKey({ nk(60, 1.0), nk(72, 1.0), nk(48, 1.0) });
    CHECK_FALSE(sameKey.valid);
}

TEST_CASE("キー候補は 5 件で score の降順になる")
{
    const std::vector<NoteForKey> notes = {
        nk(60, 0.5), nk(62, 0.5), nk(64, 0.5), nk(65, 0.5),
        nk(67, 0.5), nk(69, 0.5), nk(71, 0.5), nk(72, 0.5),
    };
    const auto r = detectKey(notes);

    REQUIRE(r.ranking.size() == 5);
    CHECK(std::is_sorted(r.ranking.begin(), r.ranking.end(), [](const KeyCandidate& a, const KeyCandidate& b) {
        return a.score > b.score;
    }));
    CHECK(r.ranking[0].root == r.root);
    CHECK(r.ranking[0].minor == r.minor);
}

TEST_CASE("confidence は 1 位と 2 位の差から 0..1 に収まる値になる")
{
    const std::vector<NoteForKey> notes = {
        nk(60, 0.5), nk(62, 0.5), nk(64, 0.5), nk(65, 0.5),
        nk(67, 0.5), nk(69, 0.5), nk(71, 0.5), nk(72, 0.5),
    };
    const auto r = detectKey(notes);
    REQUIRE(r.valid);
    REQUIRE(r.ranking.size() >= 2);

    const double gap = static_cast<double>(r.ranking[0].score) - static_cast<double>(r.ranking[1].score);
    const double expected = std::min(1.0, std::max(0.0, gap / 0.2));
    CHECK_THAT(r.confidence, WithinAbs(expected, 1e-5));
}

//==============================================================================
// MidiMatcher

TEST_CASE("完全に一致する音列は全てのペアが対応付けられる")
{
    const std::vector<TimedNote> a = { tn(0.0, 0.8, 60), tn(1.0, 1.8, 62), tn(2.0, 2.8, 64) };
    const auto r = matchNotes(a, a);

    REQUIRE(r.pairs.size() == 3);
    for (std::size_t k = 0; k < 3; ++k)
    {
        CHECK(r.pairs[k].first == static_cast<int>(k));
        CHECK(r.pairs[k].second == static_cast<int>(k));
    }
    CHECK(r.unmatchedAnalysis.empty());
    CHECK(r.unmatchedTarget.empty());
}

TEST_CASE("全体が +0.1 秒ずれていても対応付けられる")
{
    const std::vector<TimedNote> target = { tn(0.0, 0.8, 60), tn(1.0, 1.8, 62), tn(2.0, 2.8, 64) };
    std::vector<TimedNote> analysed = target;
    for (auto& n : analysed)
    {
        n.startSec += 0.1;
        n.endSec += 0.1;
    }
    const auto r = matchNotes(analysed, target);

    REQUIRE(r.pairs.size() == 3);
    CHECK(r.unmatchedAnalysis.empty());
    CHECK(r.unmatchedTarget.empty());
}

TEST_CASE("解析側に余分な音があれば unmatchedAnalysis に入る")
{
    const std::vector<TimedNote> target = { tn(0.0, 0.8, 60), tn(1.0, 1.8, 62), tn(2.0, 2.8, 64) };
    const std::vector<TimedNote> analysed = {
        tn(0.0, 0.8, 60), tn(1.0, 1.8, 62), tn(2.0, 2.8, 64), tn(5.0, 5.8, 67),
    };
    const auto r = matchNotes(analysed, target);

    REQUIRE(r.pairs.size() == 3);
    REQUIRE(r.unmatchedAnalysis.size() == 1);
    CHECK(r.unmatchedAnalysis[0] == 3);
    CHECK(r.unmatchedTarget.empty());
}

TEST_CASE("ターゲット側に余分な音があれば unmatchedTarget に入る")
{
    const std::vector<TimedNote> target = {
        tn(0.0, 0.8, 60), tn(1.0, 1.8, 62), tn(2.0, 2.8, 64), tn(5.0, 5.8, 67),
    };
    const std::vector<TimedNote> analysed = { tn(0.0, 0.8, 60), tn(1.0, 1.8, 62), tn(2.0, 2.8, 64) };
    const auto r = matchNotes(analysed, target);

    REQUIRE(r.pairs.size() == 3);
    CHECK(r.unmatchedAnalysis.empty());
    REQUIRE(r.unmatchedTarget.size() == 1);
    CHECK(r.unmatchedTarget[0] == 3);
}

TEST_CASE("12 半音違う音高のペアは対応付けない")
{
    const std::vector<TimedNote> analysed = { tn(0.0, 1.0, 60) };
    const std::vector<TimedNote> target = { tn(0.0, 1.0, 72) };
    const auto r = matchNotes(analysed, target);

    CHECK(r.pairs.empty());
    CHECK(r.unmatchedAnalysis.size() == 1);
    CHECK(r.unmatchedTarget.size() == 1);
}

TEST_CASE("順序が交差する対応付けは作らない")
{
    // 解析側 0 はターゲット 1、解析側 1 はターゲット 0 と近いが、両方を単調に対応付けることはできない
    const std::vector<TimedNote> analysed = { tn(0.0, 0.4, 60), tn(0.4, 0.8, 67) };
    const std::vector<TimedNote> target = { tn(0.1, 0.5, 67), tn(0.3, 0.7, 60) };
    const auto r = matchNotes(analysed, target);

    CHECK(r.pairs.size() <= 1);
    for (std::size_t k = 1; k < r.pairs.size(); ++k)
    {
        CHECK(r.pairs[k - 1].first < r.pairs[k].first);
        CHECK(r.pairs[k - 1].second < r.pairs[k].second);
    }
}

TEST_CASE("空入力では全て unmatched になる")
{
    const auto both = matchNotes({}, {});
    CHECK(both.pairs.empty());
    CHECK(both.unmatchedAnalysis.empty());
    CHECK(both.unmatchedTarget.empty());

    const std::vector<TimedNote> analysed = { tn(0.0, 1.0, 60), tn(1.0, 2.0, 62) };
    const auto onlyAnalysed = matchNotes(analysed, {});
    CHECK(onlyAnalysed.pairs.empty());
    CHECK(onlyAnalysed.unmatchedAnalysis.size() == 2);
    CHECK(onlyAnalysed.unmatchedTarget.empty());

    const auto onlyTarget = matchNotes({}, analysed);
    CHECK(onlyTarget.pairs.empty());
    CHECK(onlyTarget.unmatchedAnalysis.empty());
    CHECK(onlyTarget.unmatchedTarget.size() == 2);
}

TEST_CASE("解析側で分割されたノートは片方だけが対応付けられ、もう片方は unmatched になる")
{
    // ターゲットの 1 ノート (0..2 秒, 60) を、解析側は (0..1) と (1..2) に分割している
    const std::vector<TimedNote> target = { tn(0.0, 2.0, 60) };
    const std::vector<TimedNote> analysed = { tn(0.0, 1.0, 60), tn(1.0, 2.0, 60) };
    const auto r = matchNotes(analysed, target);

    REQUIRE(r.pairs.size() == 1);
    CHECK(r.pairs[0].first == 0);
    CHECK(r.pairs[0].second == 0);
    REQUIRE(r.unmatchedAnalysis.size() == 1);
    CHECK(r.unmatchedAnalysis[0] == 1);
    CHECK(r.unmatchedTarget.empty());
}

TEST_CASE("1000 ノート同士の対応付けは Release で 1 秒未満に収まる")
{
    constexpr int kCount = 1000;
    std::vector<TimedNote> target;
    std::vector<TimedNote> analysed;
    target.reserve(kCount);
    analysed.reserve(kCount);
    for (int k = 0; k < kCount; ++k)
    {
        const double start = 0.5 * k;
        const float midi = static_cast<float>(60 + (k % 12));
        target.push_back(tn(start, start + 0.4, midi));
        // わずかなゆらぎを加えた解析側
        analysed.push_back(tn(start + 0.02, start + 0.42, midi));
    }

    const auto t0 = std::chrono::steady_clock::now();
    const auto r = matchNotes(analysed, target);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    CHECK(r.pairs.size() == static_cast<std::size_t>(kCount));
    CHECK(elapsed < 1.0);
}
