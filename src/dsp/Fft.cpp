#include "dsp/Fft.h"

#include <cmath>
#include <utility>

namespace koenote::dsp
{
std::size_t nextPowerOfTwo(std::size_t n) noexcept
{
    std::size_t p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

Fft::Fft(std::size_t sizeIn) : n(nextPowerOfTwo(sizeIn < 2 ? 2 : sizeIn))
{
    bitReverse.resize(n);
    std::size_t bits = 0;
    while ((static_cast<std::size_t>(1) << bits) < n)
        ++bits;

    for (std::size_t i = 0; i < n; ++i)
    {
        std::size_t r = 0;
        for (std::size_t b = 0; b < bits; ++b)
            if (i & (static_cast<std::size_t>(1) << b))
                r |= static_cast<std::size_t>(1) << (bits - 1 - b);
        bitReverse[i] = r;
    }

    constexpr double kPi = 3.14159265358979323846;
    twiddle.resize(n / 2);
    for (std::size_t k = 0; k < n / 2; ++k)
        twiddle[k] = std::polar(1.0, -2.0 * kPi * static_cast<double>(k) / static_cast<double>(n));
}

void Fft::forward(std::complex<double>* data) const noexcept
{
    for (std::size_t i = 0; i < n; ++i)
        if (i < bitReverse[i])
            std::swap(data[i], data[bitReverse[i]]);

    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const std::size_t half = len / 2;
        const std::size_t step = n / len;

        for (std::size_t start = 0; start < n; start += len)
        {
            for (std::size_t k = 0; k < half; ++k)
            {
                const auto w = twiddle[k * step];
                const auto a = data[start + k];
                const auto b = data[start + k + half] * w;
                data[start + k] = a + b;
                data[start + k + half] = a - b;
            }
        }
    }
}
} // namespace koenote::dsp
