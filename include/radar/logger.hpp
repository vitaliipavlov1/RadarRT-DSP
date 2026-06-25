#pragma once

#include "radar/clock.hpp"
#include "radar/config.hpp"
#include "radar/heartbeat.hpp"
#include "radar/log_record.hpp"
#include "radar/log_sink.hpp"
#include "radar/managed_thread.hpp"
#include "radar/metrics.hpp"
#include "radar/realtime_manager.hpp"
#include "radar/shutdown_token.hpp"
#include "radar/thread_safe_queue.hpp"

#include <memory>
#include <vector>

namespace radar {

// Asynchronous logger (ARCHITECTURE.md D8). On its own thread it drains LogRecords
// from the output queue, formats each into a text line — the only place any
// formatting happens — and writes the line to every owned ILogSink. DSP workers
// never format or perform I/O. The output queue is DropOldest, so logging never
// blocks the DSP path; logging is best-effort. Sinks are flushed once the queue is
// closed and drained. Refreshes the approximate output queue depth gauge on the
// injected Metrics after each drained record.
//
// Owns its draining thread and its sinks; non-copyable and non-movable.
class Logger {
public:
    Logger(const Config& config, const Clock& clock, ShutdownToken& shutdownToken,
           ThreadSafeQueue<LogRecord>& outputQueue, const RealtimeManager& realtimeManager,
           Heartbeat& heartbeat, std::vector<std::unique_ptr<ILogSink>> sinks, Metrics& metrics);

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;
    ~Logger() = default;

    void start();
    void join();

private:
    void run();
    void drainLoop();

    const Config& config_;
    const Clock& clock_;
    ShutdownToken& shutdownToken_;
    ThreadSafeQueue<LogRecord>& outputQueue_;
    const RealtimeManager& realtimeManager_;
    Heartbeat& heartbeat_;
    std::vector<std::unique_ptr<ILogSink>> sinks_;
    Metrics& metrics_;
    ManagedThread thread_;
};

}  // namespace radar
