#include "radar/thread_pool.hpp"

#include "radar/diagnostics.hpp"
#include "radar/processed_signal.hpp"

#include <cstddef>
#include <optional>
#include <string>

namespace radar {

namespace {

// Turn a DSP result into a structured log record. No string formatting and no
// allocation (the message is left empty), so the realtime DSP path stays
// allocation-free for the sake of logging (D8).
LogRecord toLogRecord(const ProcessedSignal& result) noexcept {
    const LogRecord::Result payload{result.targetDetected(), result.peakMagnitude()};
    return LogRecord(LogLevel::Info, result.timestamp(), LogSource::Dsp, payload);
}

}  // namespace

ThreadPool::ThreadPool(const Config& config, const Clock& clock, ShutdownToken& shutdownToken,
                       ThreadSafeQueue<RadarSignal>& inputQueue,
                       ThreadSafeQueue<LogRecord>& outputQueue,
                       const RealtimeManager& realtimeManager, HeartbeatRegistry& heartbeatRegistry,
                       Metrics& metrics)
    : config_(config), clock_(clock), shutdownToken_(shutdownToken), inputQueue_(inputQueue),
      outputQueue_(outputQueue), realtimeManager_(realtimeManager), metrics_(metrics) {
    const std::size_t workerCount = config_.pool().workerCount;
    heartbeats_.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; ++i) {
        // Static registration before any worker thread starts (D3).
        heartbeats_.push_back(
            &heartbeatRegistry.registerHeartbeat("dsp-worker-" + std::to_string(i)));
    }
}

void ThreadPool::start() {
    threads_.reserve(heartbeats_.size());
    for (std::size_t i = 0; i < heartbeats_.size(); ++i) {
        threads_.emplace_back(&ThreadPool::workerLoop, this, i);
    }
}

void ThreadPool::join() {
    for (ManagedThread& thread : threads_) {
        thread.join();
    }
}

void ThreadPool::workerLoop(std::size_t index) {
    Heartbeat& heartbeat = *heartbeats_[index];
    try {
        // Per-thread realtime stage (D2), applied from inside this thread. Graceful
        // degradation: on failure (e.g. missing CAP_SYS_NICE) a warning is emitted and
        // the worker continues at default scheduling rather than aborting.
        const RealtimeResult realtimeStatus =
            realtimeManager_.configureCurrentThread(config_.realtime().dspPriority);
        reportRealtimeDegradation(realtimeStatus, "dsp-worker");

        while (true) {
            heartbeat.markWaiting();
            std::optional<RadarSignal> signal = inputQueue_.waitAndPop();
            if (!signal) {
                break;  // input queue closed and drained: graceful exit
            }
            metrics_.setInputQueueDepth(inputQueue_.size());
            heartbeat.markRunning(clock_.now());

            const ProcessedSignal result = processor_.process(*signal);
            metrics_.recordSignalProcessed();
            metrics_.recordProcessingLatency(clock_.now() - result.timestamp());
            // DropOldest output queue: never blocks the DSP worker (D5 / D8). The
            // return is ignored because logging is best-effort; a DropOldest
            // eviction is not visible in that return, so the drop total is read
            // from the queue itself and mirrored into Metrics (D5: counted, never
            // silent), the same way the depth gauge mirrors size().
            (void)outputQueue_.push(toLogRecord(result));
            metrics_.setOutputQueueDepth(outputQueue_.size());
            metrics_.setLogDrops(outputQueue_.droppedCount());
        }
    } catch (...) {
        // Top-level exception boundary (D7): never let an exception escape the
        // thread. Log it (best-effort via stderr), record it in Metrics, then request
        // graceful shutdown so the rest of the system winds down.
        reportUnhandledException("dsp-worker");
        metrics_.recordThreadException();
        shutdownToken_.requestShutdown();
    }
}

}  // namespace radar
