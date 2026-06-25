#pragma once

#include "radar/fft.hpp"

#include <cstddef>

namespace radar {

// Stateless threshold detector, the final DSP pipeline stage. It flags a target
// when the spectral peak rises sufficiently above the mean noise floor (a simple
// CFAR-style rule). Holds no data members, so a single instance is safe to use
// concurrently across DSP workers.
class TargetDetection {
public:
    struct Detection {
        bool targetDetected{false};
        float peakMagnitude{0.0F};
    };

    [[nodiscard]] Detection apply(const FFT::Spectrum& spectrum) const noexcept {
        float peak = 0.0F;
        float sum = 0.0F;
        for (const float magnitude : spectrum) {
            sum += magnitude;
            if (magnitude > peak) {
                peak = magnitude;
            }
        }
        const float mean = sum / static_cast<float>(spectrum.size());
        constexpr float kDetectionFactor = 4.0F;
        const bool detected = peak > kDetectionFactor * mean;
        return Detection{detected, peak};
    }
};

}  // namespace radar
