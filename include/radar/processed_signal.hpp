#pragma once

#include "radar/clock.hpp"

namespace radar {

// Result of running a RadarSignal through the DSP pipeline. Carries the source
// signal's timestamp (for correlation and latency) and the detection outcome.
// Trivially-copyable value type.
class ProcessedSignal {
public:
    using Timestamp = Clock::TimePoint;

    ProcessedSignal() = default;
    ProcessedSignal(Timestamp timestamp, bool targetDetected, float peakMagnitude) noexcept
        : timestamp_(timestamp), peakMagnitude_(peakMagnitude), targetDetected_(targetDetected) {}

    [[nodiscard]] Timestamp timestamp() const noexcept { return timestamp_; }
    [[nodiscard]] bool targetDetected() const noexcept { return targetDetected_; }
    [[nodiscard]] float peakMagnitude() const noexcept { return peakMagnitude_; }

private:
    Timestamp timestamp_{};
    float peakMagnitude_{};
    bool targetDetected_{false};
};

}  // namespace radar
