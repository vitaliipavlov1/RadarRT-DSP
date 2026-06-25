#include "radar/fft.hpp"

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <utility>

namespace radar {

namespace {
constexpr float kPi = 3.14159265358979323846F;
}  // namespace

static_assert((kRadarSampleCount & (kRadarSampleCount - 1)) == 0,
              "radix-2 FFT requires a power-of-two sample count");

FFT::Spectrum FFT::apply(const RadarSignal::SampleBuffer& samples) const noexcept {
    constexpr std::size_t n = kRadarSampleCount;
    std::array<std::complex<float>, n> data;
    for (std::size_t i = 0; i < n; ++i) {
        data[i] = std::complex<float>(samples[i], 0.0F);
    }

    // Bit-reversal permutation (in place).
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1U;
        for (; (j & bit) != 0U; bit >>= 1U) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    // Iterative radix-2 Cooley-Tukey (forward transform).
    for (std::size_t len = 2; len <= n; len <<= 1U) {
        const float angle = -2.0F * kPi / static_cast<float>(len);
        const std::complex<float> wLen(std::cos(angle), std::sin(angle));
        for (std::size_t base = 0; base < n; base += len) {
            std::complex<float> w(1.0F, 0.0F);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> even = data[base + k];
                const std::complex<float> odd = data[base + k + len / 2] * w;
                data[base + k] = even + odd;
                data[base + k + len / 2] = even - odd;
                w *= wLen;
            }
        }
    }

    // Magnitude of the lower half of the spectrum, normalized by the transform size.
    Spectrum spectrum{};
    for (std::size_t i = 0; i < spectrum.size(); ++i) {
        spectrum[i] = std::abs(data[i]) / static_cast<float>(n);
    }
    return spectrum;
}

}  // namespace radar
