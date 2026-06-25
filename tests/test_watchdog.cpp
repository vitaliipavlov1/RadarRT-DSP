// Unit tests for the Watchdog flagging policy.
//
// Determinism: heartbeat age is driven by ManualClock, so whether a heartbeat is
// "stale" is decided by the test, not by wall-clock timing. The Watchdog's scan
// cadence is inherently real-time (it waits on the ShutdownToken between scans),
// so the tests synchronize on observable effects — the diagnostic LogRecord the
// Watchdog pushes onto the output queue, and the ShutdownToken it sets on
// escalation — using blocking waits rather than sleeps. The scan interval is set
// tiny purely to keep latency low; correctness never depends on its value.
//
// Realtime is disabled in the test Config so the Watchdog thread does not attempt
// SCHED_FIFO configuration in the test environment.

#include "radar/clock.hpp"
#include "radar/config.hpp"
#include "radar/heartbeat.hpp"
#include "radar/heartbeat_registry.hpp"
#include "radar/log_record.hpp"
#include "radar/manual_clock.hpp"
#include "radar/metrics.hpp"
#include "radar/realtime_manager.hpp"
#include "radar/shutdown_token.hpp"
#include "radar/thread_safe_queue.hpp"
#include "radar/watchdog.hpp"
#include "test_framework.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

using radar::Clock;
using radar::Config;
using radar::Heartbeat;
using radar::HeartbeatRegistry;
using radar::LogLevel;
using radar::LogRecord;
using radar::LogSource;
using radar::ManualClock;
using radar::Metrics;
using radar::OverflowPolicy;
using radar::RealtimeManager;
using radar::ShutdownToken;
using radar::ThreadSafeQueue;
using radar::Watchdog;

namespace {

constexpr auto kLivenessTimeout = std::chrono::milliseconds(100);

Config makeWatchdogConfig(bool escalateToShutdown) {
    Config::Params params;
    params.realtime.enabled = false;  // no SCHED_FIFO attempts under test
    params.realtime.lockMemory = false;
    params.watchdog.scanInterval = std::chrono::milliseconds(1);
    params.watchdog.livenessTimeout = kLivenessTimeout;
    params.watchdog.escalateToShutdown = escalateToShutdown;
    return Config(params);
}

}  // namespace

RADAR_TEST(watchdog, flags_running_heartbeat_older_than_timeout) {
    const Config config = makeWatchdogConfig(/*escalateToShutdown=*/false);
    ManualClock clock;
    ShutdownToken token;
    HeartbeatRegistry registry;
    Metrics metrics;
    RealtimeManager realtime(config);
    ThreadSafeQueue<LogRecord> output(64, OverflowPolicy::DropOldest);

    Heartbeat& worker = registry.registerHeartbeat("worker");
    worker.markRunning(clock.now());                                  // running at t = 0
    clock.advance(kLivenessTimeout + std::chrono::milliseconds(50));  // now stale

    Watchdog watchdog(config, clock, token, registry, output, realtime, metrics);
    watchdog.start();

    // Blocks until the Watchdog reports the stall; no sleep involved.
    std::optional<LogRecord> record = output.waitAndPop();
    const bool escalatedByWatchdog = token.shutdownRequested();
    token.requestShutdown();  // end scanning
    watchdog.join();

    RADAR_ASSERT(record.has_value());
    RADAR_EXPECT(record->source() == LogSource::Watchdog);
    RADAR_EXPECT(record->level() == LogLevel::Error);
    RADAR_EXPECT(record->message().find("worker") != std::string::npos);
    RADAR_EXPECT(metrics.watchdogStalls() >= std::uint64_t{1});
    RADAR_EXPECT(!escalatedByWatchdog);  // report-only policy must not request shutdown
}

RADAR_TEST(watchdog, waiting_and_fresh_heartbeats_are_never_flagged) {
    const Config config = makeWatchdogConfig(/*escalateToShutdown=*/false);
    ManualClock clock;
    ShutdownToken token;
    HeartbeatRegistry registry;
    Metrics metrics;
    RealtimeManager realtime(config);
    ThreadSafeQueue<LogRecord> output(64, OverflowPolicy::DropOldest);

    Heartbeat& waiting = registry.registerHeartbeat("waiting");
    Heartbeat& fresh = registry.registerHeartbeat("fresh");
    Heartbeat& stalled = registry.registerHeartbeat("stalled");

    waiting.markRunning(clock.now());
    waiting.markWaiting();             // legitimately blocked on input
    stalled.markRunning(clock.now());  // running at t = 0 -> will go stale

    clock.advance(kLivenessTimeout + std::chrono::milliseconds(50));
    fresh.markRunning(clock.now());  // running "now" -> age 0, always fresh

    Watchdog watchdog(config, clock, token, registry, output, realtime, metrics);
    watchdog.start();

    // Every scan flags only "stalled". Drain several reports: if WaitingForInput
    // or a fresh Running heartbeat were ever flagged, a report would name it.
    for (int i = 0; i < 5; ++i) {
        std::optional<LogRecord> record = output.waitAndPop();
        RADAR_ASSERT(record.has_value());
        RADAR_EXPECT(record->message().find("stalled") != std::string::npos);
        RADAR_EXPECT(record->message().find("waiting") == std::string::npos);
        RADAR_EXPECT(record->message().find("fresh") == std::string::npos);
    }

    token.requestShutdown();
    watchdog.join();
}

RADAR_TEST(watchdog, escalates_to_shutdown_when_configured) {
    const Config config = makeWatchdogConfig(/*escalateToShutdown=*/true);
    ManualClock clock;
    ShutdownToken token;
    HeartbeatRegistry registry;
    Metrics metrics;
    RealtimeManager realtime(config);
    ThreadSafeQueue<LogRecord> output(64, OverflowPolicy::DropOldest);

    Heartbeat& worker = registry.registerHeartbeat("worker");
    worker.markRunning(clock.now());
    clock.advance(kLivenessTimeout + std::chrono::milliseconds(50));

    Watchdog watchdog(config, clock, token, registry, output, realtime, metrics);
    watchdog.start();

    // The escalating Watchdog must set the token; this blocks until it does.
    const bool requested = token.waitFor(std::chrono::seconds(5));
    watchdog.join();

    RADAR_EXPECT(requested);
    RADAR_EXPECT(token.shutdownRequested());
    RADAR_EXPECT(metrics.watchdogStalls() >= std::uint64_t{1});
}
