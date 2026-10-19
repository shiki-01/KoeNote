#pragma once

#include <vector>

namespace koenote::dsp
{
// 窓付き sinc によるサンプルレート変換（モノラル）。入力が空、またはレートが不正なら空を返す。
// 出力の長さは round(in.size() * dstRate / srcRate)。ダウンサンプル時は出力の Nyquist でローパスする。
std::vector<float> resample(const std::vector<float>& in, double srcRate, double dstRate);
} // namespace koenote::dsp
