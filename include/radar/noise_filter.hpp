#pragma once

#include "radar/radar_signal.hpp"

#include <cstddef>

namespace radar {

// Stateless 3-tap moving-average noise filter, the first DSP pipeline stage. It
// holds no data members, so a single instance is safe to use concurrently across
// DSP workers. A new stage is added by composition in SignalProcessor, not by
// inheritance.
class NoiseFilter {
public:
    [[nodiscard]] RadarSignal::SampleBuffer
    apply(const RadarSignal::SampleBuffer& samples) const noexcept {
        RadarSignal::SampleBuffer filtered{};
        const std::size_t n = samples.size();
        for (std::size_t i = 0; i < n; ++i) {
            float sum = samples[i];
            std::size_t count = 1;
            if (i > 0) {
                sum += samples[i - 1];
                ++count;
            }
            if (i + 1 < n) {
                sum += samples[i + 1];
                ++count;
            }
            filtered[i] = sum / static_cast<float>(count);
        }
        return filtered;
    }
};

}  // namespace radar
