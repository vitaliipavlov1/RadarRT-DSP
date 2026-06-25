#include "radar/clock.hpp"

#include "radar/manual_clock.hpp"
#include "radar/steady_clock.hpp"

#include <chrono>

namespace radar {

Clock::TimePoint SteadyClock::now() const noexcept {
    return std::chrono::steady_clock::now();
}

namespace {

std::chrono::nanoseconds::rep toNanos(Clock::TimePoint timePoint) noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(timePoint.time_since_epoch())
        .count();
}

}  // namespace

ManualClock::ManualClock(TimePoint start) noexcept : nanos_{toNanos(start)} {}

Clock::TimePoint ManualClock::now() const noexcept {
    const std::chrono::nanoseconds elapsed{nanos_.load(std::memory_order_acquire)};
    return TimePoint{std::chrono::duration_cast<Duration>(elapsed)};
}

void ManualClock::advance(Duration delta) noexcept {
    const auto deltaNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count();
    nanos_.fetch_add(deltaNanos, std::memory_order_release);
}

}  // namespace radar
