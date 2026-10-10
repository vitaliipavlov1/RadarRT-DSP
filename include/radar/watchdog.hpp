#pragma once

#include "radar/clock.hpp"
#include "radar/config.hpp"
#include "radar/heartbeat_registry.hpp"
#include "radar/log_record.hpp"
#include "radar/managed_thread.hpp"
#include "radar/metrics.hpp"
#include "radar/realtime_manager.hpp"
#include "radar/shutdown_token.hpp"
#include "radar/thread_safe_queue.hpp"

#include <string>

namespace radar {

// Health supervisor (ARCHITECTURE.md D3). On its own thread it periodically scans
// the HeartbeatRegistry and flags any worker whose heartbeat is Running and whose
// age exceeds the configured liveness timeout. A heartbeat in WaitingForInput (a
// worker legitimately blocked on an empty queue) is never flagged. Age is measured
// with the injected Clock. The registry owns the heartbeats for the whole
// application lifetime and the Watchdog only reads them, lock-free (D3); hence
// it borrows the registry as a const reference.
//
// The inter-scan delay is waited out on the ShutdownToken rather than on a bare
// sleep (D4), so a shutdown request ends scanning immediately, before the ordered
// drain begins — the intentional drain is never mistaken for a stall. The Watchdog
// has no Heartbeat of its own: it is the monitor, not a monitored worker.
//
// A detected stall is reported as a diagnostic LogRecord enqueued on the shared
// output queue, which the Logger renders (D8); the push is best-effort and never
// blocks. Policy is configurable through Config: report-only (the default) or,
// additionally, request a graceful shutdown via the ShutdownToken. A thread
// genuinely hung in Running state cannot be force-recovered — the Watchdog reports
// it and, if so configured, requests shutdown; no abort or forced-termination
// machinery is introduced. Every detected stall is also counted on the
// injected Metrics.
//
// Owns its scanning thread and references borrowed state: non-copyable and
// non-movable.
class Watchdog {
public:
    Watchdog(const Config& config, const Clock& clock, ShutdownToken& shutdownToken,
             const HeartbeatRegistry& heartbeatRegistry, ThreadSafeQueue<LogRecord>& outputQueue,
             const RealtimeManager& realtimeManager, Metrics& metrics);

    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;
    Watchdog(Watchdog&&) = delete;
    Watchdog& operator=(Watchdog&&) = delete;
    ~Watchdog() = default;

    void start();
    void join();

private:
    void run();
    void scanLoop();
    void scanOnce();
    void reportStall(const std::string& name, Clock::Duration age, Clock::TimePoint now);

    const Config& config_;
    const Clock& clock_;
    ShutdownToken& shutdownToken_;
    const HeartbeatRegistry& heartbeatRegistry_;
    ThreadSafeQueue<LogRecord>& outputQueue_;
    const RealtimeManager& realtimeManager_;
    Metrics& metrics_;
    ManagedThread thread_;
};

}  // namespace radar
