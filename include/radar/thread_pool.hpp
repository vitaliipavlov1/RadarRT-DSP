#pragma once

#include "radar/clock.hpp"
#include "radar/config.hpp"
#include "radar/heartbeat.hpp"
#include "radar/heartbeat_registry.hpp"
#include "radar/log_record.hpp"
#include "radar/managed_thread.hpp"
#include "radar/metrics.hpp"
#include "radar/radar_signal.hpp"
#include "radar/realtime_manager.hpp"
#include "radar/shutdown_token.hpp"
#include "radar/signal_processor.hpp"
#include "radar/thread_safe_queue.hpp"

#include <cstddef>
#include <vector>

namespace radar {

// DSP consumer pool (ARCHITECTURE.md D1). It owns N worker threads and nothing
// else: there is no internal task queue. Each worker pulls a RadarSignal directly
// from the input queue, runs the shared stateless SignalProcessor, turns the
// result into a structured LogRecord, and pushes it to the output queue (which is
// configured DropOldest, so the push never blocks the worker). Each worker owns
// one Heartbeat. Workers exit when the input queue is closed and drained.
// Each worker reports signals processed and a sampled end-to-end processing
// latency to the injected Metrics, and refreshes the approximate input/output
// queue depth gauges after the operations that change them.
//
// Owns its threads and references escape into them: non-copyable and non-movable.
class ThreadPool {
public:
    ThreadPool(const Config& config, const Clock& clock, ShutdownToken& shutdownToken,
               ThreadSafeQueue<RadarSignal>& inputQueue, ThreadSafeQueue<LogRecord>& outputQueue,
               const RealtimeManager& realtimeManager, HeartbeatRegistry& heartbeatRegistry,
               Metrics& metrics);

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;
    ~ThreadPool() = default;

    void start();
    void join();

private:
    void workerLoop(std::size_t index);

    const Config& config_;
    const Clock& clock_;
    ShutdownToken& shutdownToken_;
    ThreadSafeQueue<RadarSignal>& inputQueue_;
    ThreadSafeQueue<LogRecord>& outputQueue_;
    const RealtimeManager& realtimeManager_;
    Metrics& metrics_;
    SignalProcessor processor_{};
    std::vector<Heartbeat*> heartbeats_;
    std::vector<ManagedThread> threads_;
};

}  // namespace radar
