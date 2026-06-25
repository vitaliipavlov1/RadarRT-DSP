#include "radar/heartbeat_registry.hpp"

#include "radar/clock.hpp"

#include <atomic>
#include <string>
#include <type_traits>
#include <utility>

namespace radar {

Heartbeat& HeartbeatRegistry::registerHeartbeat(std::string name) {
    entries_.emplace_back(std::move(name));
    return entries_.back().heartbeat;
}

// The Watchdog's reads must be lock-free (D3): enforce it for the platform.
static_assert(std::atomic<Heartbeat::TimePoint::rep>::is_always_lock_free);
static_assert(std::atomic<HeartbeatState>::is_always_lock_free);

// Heartbeats and the registry are owned in place and referenced elsewhere, so
// neither may be copied or moved.
static_assert(!std::is_copy_constructible_v<Heartbeat>);
static_assert(!std::is_move_constructible_v<Heartbeat>);
static_assert(!std::is_copy_constructible_v<HeartbeatRegistry>);
static_assert(!std::is_move_constructible_v<HeartbeatRegistry>);

namespace {

// Compile-time check that the worker and Watchdog usage patterns instantiate
// cleanly under the project warning set (the forEach template is otherwise only
// type-checked when used). Never executed.
[[maybe_unused]] void compileCheck() {
    HeartbeatRegistry registry;
    Heartbeat& heartbeat = registry.registerHeartbeat("compile-check");
    heartbeat.markRunning(Clock::TimePoint{});
    heartbeat.markWaiting();
    registry.forEach([](const std::string&, const Heartbeat& observed) {
        (void)observed.state();
        (void)observed.timestamp();
    });
    (void)registry.size();
}

}  // namespace

}  // namespace radar
