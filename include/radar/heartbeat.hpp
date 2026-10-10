#pragma once

#include "radar/clock.hpp"

#include <atomic>

namespace radar {

// Liveness state a worker reports through its Heartbeat.
enum class HeartbeatState {
    Running,          // actively executing work
    WaitingForInput,  // legitimately blocked waiting for work (never flagged)
};

// Per-thread liveness signal (ARCHITECTURE.md D3). A worker reports it is alive by
// marking itself Running and refreshing the timestamp each loop iteration / on
// wake, and marks itself WaitingForInput before it blocks on an empty queue. The
// Watchdog reads it without locking and treats only a Running heartbeat older than
// the timeout as stalled; WaitingForInput is never flagged.
//
// The initial state is WaitingForInput so a registered-but-not-yet-started worker
// is never mistaken for a stall before its first beat.
//
// Owned by HeartbeatRegistry and borrowed by reference; non-copyable and
// non-movable.
class Heartbeat {
public:
    using TimePoint = Clock::TimePoint;

    Heartbeat() noexcept = default;
    ~Heartbeat() = default;

    Heartbeat(const Heartbeat&) = delete;
    Heartbeat& operator=(const Heartbeat&) = delete;
    Heartbeat(Heartbeat&&) = delete;
    Heartbeat& operator=(Heartbeat&&) = delete;

    // Worker side: report Running and refresh the liveness timestamp. The timestamp
    // is published before the state, so a Watchdog that observes Running also
    // observes the matching (or newer) timestamp.
    void markRunning(TimePoint now) noexcept {
        timestamp_.store(now.time_since_epoch().count(), std::memory_order_release);
        state_.store(HeartbeatState::Running, std::memory_order_release);
    }

    // Worker side: report a legitimate wait (not a stall).
    void markWaiting() noexcept {
        state_.store(HeartbeatState::WaitingForInput, std::memory_order_release);
    }

    // Watchdog side: lock-free reads. Read state() BEFORE timestamp(): markRunning
    // publishes the timestamp before the state under release ordering, so an acquire
    // load that observes Running is guaranteed to then observe the matching (or a
    // newer) timestamp. Reading them in the opposite order could pair a fresh
    // Running with a stale timestamp and report a false stall.
    [[nodiscard]] HeartbeatState state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
    [[nodiscard]] TimePoint timestamp() const noexcept {
        return TimePoint{TimePoint::duration{timestamp_.load(std::memory_order_acquire)}};
    }

private:
    std::atomic<TimePoint::rep> timestamp_{0};
    std::atomic<HeartbeatState> state_{HeartbeatState::WaitingForInput};
};

}  // namespace radar
