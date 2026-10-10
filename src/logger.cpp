#include "radar/logger.hpp"

#include "radar/diagnostics.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace radar {

namespace {

const char* levelName(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }
    return "?";
}

const char* sourceName(LogSource source) noexcept {
    switch (source) {
        case LogSource::Producer:
            return "Producer";
        case LogSource::Dsp:
            return "Dsp";
        case LogSource::Watchdog:
            return "Watchdog";
    }
    return "?";
}

// The single place where a LogRecord becomes text (D8). Runs on the Logger thread,
// off the realtime path, so allocation here is acceptable.
std::string formatLine(const LogRecord& record) {
    std::ostringstream out;
    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(record.timestamp().time_since_epoch())
            .count();
    out << "[t=" << micros << "us] [" << levelName(record.level()) << "] ["
        << sourceName(record.source()) << "] ";
    if (record.result().has_value()) {
        const LogRecord::Result& result = *record.result();
        out << "detected=" << (result.targetDetected ? 1 : 0) << " peak=" << result.peakMagnitude;
    } else {
        out << record.message();
    }
    out << '\n';
    return out.str();
}

}  // namespace

Logger::Logger(const Config& config, const Clock& clock, ShutdownToken& shutdownToken,
               ThreadSafeQueue<LogRecord>& outputQueue, const RealtimeManager& realtimeManager,
               Heartbeat& heartbeat, std::vector<std::unique_ptr<ILogSink>> sinks, Metrics& metrics)
    : config_(config), clock_(clock), shutdownToken_(shutdownToken), outputQueue_(outputQueue),
      realtimeManager_(realtimeManager), heartbeat_(heartbeat), sinks_(std::move(sinks)),
      metrics_(metrics) {}

void Logger::start() {
    thread_ = ManagedThread(&Logger::run, this);
}

void Logger::join() {
    thread_.join();
}

void Logger::run() {
    try {
        drainLoop();
    } catch (...) {
        // Top-level exception boundary (D7): never let an exception escape the
        // thread. Log it (best-effort via stderr — the Logger cannot rely on itself
        // here), record it in Metrics, then request graceful shutdown so the rest of
        // the system winds down.
        reportUnhandledException("logger");
        metrics_.recordThreadException();
        shutdownToken_.requestShutdown();
    }
}

void Logger::drainLoop() {
    // Per-thread realtime stage (D2), applied from inside this thread. Graceful
    // degradation: on failure (e.g. missing CAP_SYS_NICE) a warning is emitted to
    // stderr (the Logger cannot route its own startup warning through itself) and the
    // thread continues at default scheduling rather than aborting.
    const RealtimeResult realtimeStatus =
        realtimeManager_.configureCurrentThread(config_.realtime().loggerPriority);
    reportRealtimeDegradation(realtimeStatus, "logger");

    while (true) {
        heartbeat_.markWaiting();
        std::optional<LogRecord> record = outputQueue_.waitAndPop();
        if (!record) {
            break;  // output queue closed and drained: graceful exit
        }
        metrics_.setOutputQueueDepth(outputQueue_.size());
        heartbeat_.markRunning(clock_.now());

        const std::string line = formatLine(*record);
        for (const std::unique_ptr<ILogSink>& sink : sinks_) {
            sink->write(line);
        }
    }

    // Flush every sink once the queue is drained (D8).
    for (const std::unique_ptr<ILogSink>& sink : sinks_) {
        sink->flush();
    }
}

}  // namespace radar
