#pragma once

#include <chrono>

namespace radar {

// Abstract monotonic time source. Injected into every time-dependent subsystem
// (producer period, heartbeat age, watchdog timeout) so that time can be driven
// deterministically in tests (see ManualClock). This is the only clock
// abstraction in the system.
//
// Non-copyable and non-movable: clocks are owned once and shared by reference.
class Clock {
public:
    using TimePoint = std::chrono::steady_clock::time_point;
    using Duration = std::chrono::steady_clock::duration;

    virtual ~Clock() = default;

    [[nodiscard]] virtual TimePoint now() const noexcept = 0;

    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;
    Clock(Clock&&) = delete;
    Clock& operator=(Clock&&) = delete;

protected:
    Clock() = default;
};

}  // namespace radar
