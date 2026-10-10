// Unit tests for Heartbeat and HeartbeatRegistry:
// the state-aware transitions (Running / WaitingForInput) and timestamp publishing,
// driven deterministically by ManualClock, plus the registry's stable-reference
// and ordered-iteration guarantees that the Watchdog relies on.

#include "radar/clock.hpp"
#include "radar/heartbeat.hpp"
#include "radar/heartbeat_registry.hpp"
#include "radar/manual_clock.hpp"
#include "test_framework.hpp"

#include <chrono>
#include <cstddef>
#include <string>

using radar::Clock;
using radar::Heartbeat;
using radar::HeartbeatRegistry;
using radar::HeartbeatState;
using radar::ManualClock;

RADAR_TEST(heartbeat, initial_state_is_waiting_for_input) {
    // A registered-but-not-yet-started worker must never look like a stall.
    Heartbeat heartbeat;
    RADAR_EXPECT(heartbeat.state() == HeartbeatState::WaitingForInput);
}

RADAR_TEST(heartbeat, mark_running_publishes_state_and_timestamp) {
    const Clock::TimePoint at = Clock::TimePoint{} + std::chrono::seconds(5);
    ManualClock clock(at);
    Heartbeat heartbeat;

    heartbeat.markRunning(clock.now());
    RADAR_EXPECT(heartbeat.state() == HeartbeatState::Running);
    RADAR_EXPECT(heartbeat.timestamp() == at);
}

RADAR_TEST(heartbeat, mark_waiting_resets_state_but_keeps_timestamp) {
    const Clock::TimePoint at = Clock::TimePoint{} + std::chrono::seconds(2);
    Heartbeat heartbeat;

    heartbeat.markRunning(at);
    heartbeat.markWaiting();
    RADAR_EXPECT(heartbeat.state() == HeartbeatState::WaitingForInput);
    // markWaiting only changes the state; the last running timestamp is retained.
    RADAR_EXPECT(heartbeat.timestamp() == at);
}

RADAR_TEST(heartbeat, registry_register_returns_stable_reference) {
    HeartbeatRegistry registry;
    Heartbeat& first = registry.registerHeartbeat("first");
    first.markRunning(Clock::TimePoint{} + std::chrono::seconds(1));

    // Subsequent registrations must not invalidate the earlier reference: the
    // registry stores entries in a deque precisely so borrowed references stay
    // valid for the application lifetime.
    for (int i = 0; i < 64; ++i) {
        (void)registry.registerHeartbeat("filler");
    }

    RADAR_EXPECT(first.state() == HeartbeatState::Running);
    RADAR_EXPECT_EQ(registry.size(), std::size_t{65});
}

RADAR_TEST(heartbeat, registry_for_each_visits_all_in_registration_order) {
    HeartbeatRegistry registry;
    (void)registry.registerHeartbeat("a");
    (void)registry.registerHeartbeat("b");
    (void)registry.registerHeartbeat("c");

    std::string order;
    std::size_t visited = 0;
    registry.forEach([&order, &visited](const std::string& name, const Heartbeat&) {
        order += name;
        ++visited;
    });

    RADAR_EXPECT_EQ(visited, std::size_t{3});
    RADAR_EXPECT_EQ(order, std::string("abc"));
}
