#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace koenote::dsp
{
// radix-2 の複素 FFT。size は 2 のべき乗。インスタンスは const で複数スレッドから使える（作業領域を持たない）。
class Fft
{
public:
    explicit Fft(std::size_t sizeIn);

    std::size_t size() const noexcept { return n; }

    // 前方変換（e^{-i...}）。data は size() 個。
    void forward(std::complex<double>* data) const noexcept;

private:
    std::size_t n;
    std::vector<std::size_t> bitReverse;
    std::vector<std::complex<double>> twiddle;
};

// n 以上の最小の 2 のべき乗
std::size_t nextPowerOfTwo(std::size_t n) noexcept;
} // namespace koenote::dsp
