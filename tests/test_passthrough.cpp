#include "render/PassthroughRenderer.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

using namespace koenote::render;

namespace
{
struct Stereo
{
    std::vector<float> left, right;
    std::array<const float*, 2> ptrs {};

    Stereo(std::initializer_list<float> l, std::initializer_list<float> r) : left(l), right(r)
    {
        ptrs = { left.data(), right.data() };
    }

    AudioView view() const { return { ptrs.data(), 2, static_cast<std::int64_t>(left.size()) }; }
};
} // namespace

TEST_CASE("パススルーは範囲内のサンプルをそのまま加算する")
{
    Stereo src({ 1, 2, 3, 4 }, { 5, 6, 7, 8 });
    std::vector<float> l(4, 0.0f), r(4, 0.0f);
    float* dst[2] = { l.data(), r.data() };

    accumulatePassthrough(src.view(), 0, dst, 2, 0, 4);

    CHECK(l == std::vector<float> { 1, 2, 3, 4 });
    CHECK(r == std::vector<float> { 5, 6, 7, 8 });
}

TEST_CASE("パススルーは範囲外を無音として扱う")
{
    Stereo src({ 1, 2, 3, 4 }, { 1, 2, 3, 4 });
    std::vector<float> l(6, 0.0f), r(6, 0.0f);
    float* dst[2] = { l.data(), r.data() };

    // 先頭 2 サンプルは元音声の開始前、末尾は終了後
    accumulatePassthrough(src.view(), -2, dst, 2, 0, 6);

    CHECK(l == std::vector<float> { 0, 0, 1, 2, 3, 4 });
}

TEST_CASE("パススルーは dstOffset 以降へ書き込み、既存の内容に加算する")
{
    Stereo src({ 1, 1 }, { 1, 1 });
    std::vector<float> l { 10, 10, 10, 10 }, r { 10, 10, 10, 10 };
    float* dst[2] = { l.data(), r.data() };

    accumulatePassthrough(src.view(), 0, dst, 2, 2, 2);

    CHECK(l == std::vector<float> { 10, 10, 11, 11 });
}

TEST_CASE("モノラル音声はステレオへ複製される")
{
    std::vector<float> mono { 1, 2, 3 };
    const float* ptrs[1] = { mono.data() };
    AudioView src { ptrs, 1, 3 };

    std::vector<float> l(3, 0.0f), r(3, 0.0f);
    float* dst[2] = { l.data(), r.data() };
    accumulatePassthrough(src, 0, dst, 2, 0, 3);

    CHECK(l == mono);
    CHECK(r == mono);
}

TEST_CASE("ステレオ音声のモノラル出力は平均になる")
{
    Stereo src({ 2, 4 }, { 6, 8 });
    std::vector<float> m(2, 0.0f);
    float* dst[1] = { m.data() };

    accumulatePassthrough(src.view(), 0, dst, 1, 0, 2);

    CHECK(m == std::vector<float> { 4, 6 });
}

TEST_CASE("元音声より後ろの範囲では何も書き込まない")
{
    Stereo src({ 1, 2 }, { 1, 2 });
    std::vector<float> l(4, 0.0f), r(4, 0.0f);
    float* dst[2] = { l.data(), r.data() };

    accumulatePassthrough(src.view(), 5, dst, 2, 0, 4);

    CHECK(l == std::vector<float>(4, 0.0f));
}
