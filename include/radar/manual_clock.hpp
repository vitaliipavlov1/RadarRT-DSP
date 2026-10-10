#pragma once

#include "radar/clock.hpp"

#include <atomic>
#include <chrono>

namespace radar {

// Test clock: time advances only when the test advances it, making time-dependent
// logic deterministic. Thread-safe, because the subsystem under test may read
// now() from its own thread while the test thread advances time.
class ManualClock final : public Clock {
public:
    explicit ManualClock(TimePoint start = TimePoint{}) noexcept;

    [[nodiscard]] TimePoint now() const noexcept override;

    void advance(Duration delta) noexcept;

private:
    std::atomic<std::chrono::nanoseconds::rep> nanos_;
};

}  // namespace radar
