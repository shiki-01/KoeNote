#include "render/TimeMap.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace koenote;
using Catch::Matchers::WithinAbs;

namespace
{
model::Note note(double s, double e)
{
    model::Note n;
    n.startSec = s;
    n.endSec = e;
    return n;
}
} // namespace

TEST_CASE("空の TimeMap は恒等写像")
{
    render::TimeMap map;
    CHECK(map.isIdentity());
    CHECK(map.toSource(1.25) == 1.25);
    CHECK(map.toOutput(3.5) == 3.5);
    CHECK(map.slopeAt(2.0) == 1.0);
}

TEST_CASE("編集のないノート列から作った TimeMap は恒等写像と同じ値を返す")
{
    const auto map = render::TimeMap::fromNotes({ note(1.0, 2.0), note(2.5, 3.0) });

    for (const double t : { 0.0, 0.5, 1.0, 1.7, 2.2, 2.7, 3.0, 4.0 })
    {
        CHECK_THAT(map.toSource(t), WithinAbs(t, 1e-9));
        CHECK_THAT(map.toOutput(t), WithinAbs(t, 1e-9));
    }
}

TEST_CASE("ノートを後ろへ伸ばすと、ノート内は伸縮し、以降は平行移動する")
{
    auto a = note(1.0, 2.0);
    a.edit.targetStartSec = 1.0;
    a.edit.targetEndSec = 3.0; // 2 倍に伸ばす
    const auto b = note(2.5, 3.5);

    // b は出力側でも 2.5 から始まるため、a の終端 3.0 と重ならないよう 3.5 へ移す
    auto b2 = b;
    b2.edit.targetStartSec = 3.5;
    b2.edit.targetEndSec = 4.5;

    const auto map = render::TimeMap::fromNotes({ a, b2 });

    CHECK_THAT(map.toSource(2.0), WithinAbs(1.5, 1e-9)); // ノート内: 傾き 0.5
    CHECK_THAT(map.slopeAt(2.0), WithinAbs(0.5, 1e-9));
    CHECK_THAT(map.toSource(4.0), WithinAbs(3.0, 1e-9)); // b の中央
    CHECK_THAT(map.toSource(5.0), WithinAbs(4.0, 1e-9)); // 最後のノートより後は平行移動（+1.0）
    CHECK_THAT(map.toOutput(3.0), WithinAbs(4.0, 1e-9));
}

TEST_CASE("ノート間の隙間は伸縮し、直後のノートの手前は子音として保護される")
{
    auto b = note(3.0, 4.0);
    b.edit.targetStartSec = 5.0; // 隙間 1.0 → 3.0 に広げる（a は 1.0〜2.0 のまま）
    b.edit.targetEndSec = 6.0;
    const auto map = render::TimeMap::fromNotes({ note(1.0, 2.0), b }, 0.06);

    // 直後のノートの手前 0.06 秒は伸縮しない（傾き 1）
    CHECK_THAT(map.slopeAt(4.97), WithinAbs(1.0, 1e-9));
    CHECK_THAT(map.toSource(4.97), WithinAbs(2.97, 1e-9));
    // それより前の隙間は残りを引き受けて引き伸ばされる
    CHECK(map.slopeAt(3.0) < 1.0);
}

TEST_CASE("最初のノートが出力側で前に動く場合も出力時刻は負にならない")
{
    auto a = note(1.0, 2.0);
    a.edit.targetStartSec = 0.2;
    a.edit.targetEndSec = 1.2;
    const auto map = render::TimeMap::fromNotes({ a });

    CHECK_THAT(map.toSource(0.2), WithinAbs(1.0, 1e-9));
    CHECK_THAT(map.toOutput(0.0), WithinAbs(-0.8, 1e-9)); // 元音声の 0 秒は出力の範囲外（負）に対応する
}

TEST_CASE("toOutput は toSource の逆写像になる")
{
    auto a = note(1.0, 2.0);
    a.edit.targetStartSec = 1.2;
    a.edit.targetEndSec = 2.6;
    const auto map = render::TimeMap::fromNotes({ a, note(3.0, 4.0) });

    for (const double out : { 0.5, 1.3, 2.0, 2.6, 3.4, 4.8 })
        CHECK_THAT(map.toOutput(map.toSource(out)), WithinAbs(out, 1e-9));
}
