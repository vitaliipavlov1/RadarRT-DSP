#pragma once

#include "radar/clock.hpp"

namespace radar {

// Production clock: monotonic system time (CLOCK_MONOTONIC via std::steady_clock).
class SteadyClock final : public Clock {
public:
    [[nodiscard]] TimePoint now() const noexcept override;
};

}  // namespace radar
