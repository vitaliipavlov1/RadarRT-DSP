#pragma once

#include "radar/radar_signal.hpp"

#include <array>
#include <cstddef>

namespace radar {

// Stateless magnitude-spectrum transform (radix-2 FFT) of a RadarSignal's samples,
// the second DSP pipeline stage. Returns the magnitudes of the lower half of the
// spectrum (the meaningful half for real input). Holds no data members, so a
// single instance is safe to use concurrently across DSP workers.
class FFT {
public:
    using Spectrum = std::array<float, kRadarSampleCount / 2>;

    [[nodiscard]] Spectrum apply(const RadarSignal::SampleBuffer& samples) const noexcept;
};

}  // namespace radar
