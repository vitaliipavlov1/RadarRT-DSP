#pragma once

#include "radar/clock.hpp"

#include <array>
#include <cstddef>

namespace radar {

// Number of raw samples carried by each radar signal. Fixed at compile time so a
// RadarSignal stores its samples inline (std::array) with no heap allocation on
// the realtime path.
inline constexpr std::size_t kRadarSampleCount = 256;

// Raw, timestamped block of radar samples produced by the RadarProducer and
// consumed by the DSP pipeline. Trivially-copyable value type.
class RadarSignal {
public:
    using SampleBuffer = std::array<float, kRadarSampleCount>;
    using Timestamp = Clock::TimePoint;

    RadarSignal() = default;
    RadarSignal(Timestamp timestamp, const SampleBuffer& samples) noexcept
        : timestamp_(timestamp), samples_(samples) {}

    [[nodiscard]] Timestamp timestamp() const noexcept { return timestamp_; }
    [[nodiscard]] const SampleBuffer& samples() const noexcept { return samples_; }

private:
    Timestamp timestamp_{};
    SampleBuffer samples_{};
};

}  // namespace radar
