#include "radar/watchdog.hpp"

#include "radar/diagnostics.hpp"
#include "radar/heartbeat.hpp"

#include <chrono>
#include <sstream>
#include <string>
#include <utility>

namespace radar {

Watchdog::Watchdog(const Config& config, const Clock& clock, ShutdownToken& shutdownToken,
                   const HeartbeatRegistry& heartbeatRegistry,
                   ThreadSafeQueue<LogRecord>& outputQueue, const RealtimeManager& realtimeManager,
                   Metrics& metrics)
    : config_(config), clock_(clock), shutdownToken_(shutdownToken),
      heartbeatRegistry_(heartbeatRegistry), outputQueue_(outputQueue),
      realtimeManager_(realtimeManager), metrics_(metrics) {}

void Watchdog::start() {
    thread_ = ManagedThread(&Watchdog::run, this);
}

void Watchdog::join() {
    thread_.join();
}

void Watchdog::run() {
    try {
        scanLoop();
    } catch (...) {
        // Top-level exception boundary (D7): never let an exception escape the
        // thread. Log it (best-effort via stderr), record it in Metrics, then request
        // graceful shutdown so the rest of the system winds down.
        reportUnhandledException("watchdog");
        metrics_.recordThreadException();
        shutdownToken_.requestShutdown();
    }
}

void Watchdog::scanLoop() {
    // Per-thread realtime stage (D2), applied from inside this thread. Graceful
    // degradation: on failure (e.g. missing CAP_SYS_NICE) a warning is emitted and the
    // thread continues at default scheduling rather than aborting.
    const RealtimeResult realtimeStatus =
        realtimeManager_.configureCurrentThread(config_.realtime().watchdogPriority);
    reportRealtimeDegradation(realtimeStatus, "watchdog");

    // Wait first, then scan. Waiting on the ShutdownToken (not a bare sleep) lets a
    // shutdown request return immediately and end scanning before the ordered drain
    // begins (D4), so the intentional drain is never mistaken for a stall. The wait
    // also rules out busy waiting.
    while (!shutdownToken_.waitFor(config_.watchdog().scanInterval)) {
        scanOnce();
    }
}

void Watchdog::scanOnce() {
    const Clock::TimePoint now = clock_.now();
    const Clock::Duration timeout = config_.watchdog().livenessTimeout;
    bool stallDetected = false;

    heartbeatRegistry_.forEach([&](const std::string& name, const Heartbeat& heartbeat) {
        // Read state() BEFORE timestamp() (see Heartbeat): an acquire load that
        // observes Running is guaranteed to then observe the matching or a newer
        // timestamp, so a fresh Running is never paired with a stale timestamp.
        if (heartbeat.state() != HeartbeatState::Running) {
            return;  // WaitingForInput is a legitimate wait, never flagged (D3)
        }
        const Clock::Duration age = now - heartbeat.timestamp();
        if (age <= timeout) {
            return;  // the beat is fresh enough
        }
        stallDetected = true;
        reportStall(name, age, now);
    });

    if (stallDetected && config_.watchdog().escalateToShutdown) {
        // Configurable escalation: a thread genuinely hung in Running state
        // cannot be force-recovered, so request a graceful shutdown. No abort or
        // forced-termination machinery is introduced.
        shutdownToken_.requestShutdown();
    }
}

void Watchdog::reportStall(const std::string& name, Clock::Duration age, Clock::TimePoint now) {
    // Diagnostic logging from a non-realtime subsystem may carry a message string
    // (D8): off the realtime path, building it here is acceptable. The Logger still
    // performs the final line formatting. Best-effort: the output queue is
    // DropOldest, so the push never blocks (a resulting eviction is counted on
    // outputQueue_.droppedCount(), which the DSP workers mirror into Metrics).
    metrics_.recordWatchdogStall();
    const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(age).count();
    std::ostringstream message;
    message << "stalled worker '" << name << "' Running for " << ageMs << "ms";
    LogRecord record(LogLevel::Error, now, LogSource::Watchdog, message.str());
    (void)outputQueue_.push(std::move(record));
}

}  // namespace radar
