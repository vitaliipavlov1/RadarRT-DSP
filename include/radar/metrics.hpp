#pragma once

#include "radar/clock.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace radar {

// Thread-safe observability counters and gauges (ARCHITECTURE.md "Metrics
// Strategy"). Every subsystem reports through here:
// RadarProducer (signals produced, deadline misses, input drops), ThreadPool
// workers (signals processed, processing latency, log drops), producer/workers/
// Logger (queue depth), Watchdog (stalls), and every thread's top-level
// exception boundary (D7).
//
// Two kinds of atomic field, both following the project-wide ordering rule
// (ARCHITECTURE.md "Synchronization Strategy": release on write, acquire on
// read), as in Heartbeat / ShutdownToken / ManualClock:
//   - event counters, incremented by the thread on which the event occurs;
//   - latest-value gauges, written with a release store. Queue-depth and
//     drop-total gauges mirror the authoritative value owned by a queue
//     (size() / droppedCount()); the latency gauge holds the most recent
//     sample. A gauge is "approximate" because it is sampled around a queue
//     operation, not atomically with it.
//
// Owns only atomics; non-copyable and non-movable, like the other
// infrastructure components that are constructed once and shared by reference.
class Metrics {
public:
    Metrics() noexcept = default;
    ~Metrics() = default;

    Metrics(const Metrics&) = delete;
    Metrics& operator=(const Metrics&) = delete;
    Metrics(Metrics&&) = delete;
    Metrics& operator=(Metrics&&) = delete;

    // Event counters: incremented where the event occurs.
    void recordSignalProduced() noexcept;
    void recordSignalProcessed() noexcept;
    void recordDeadlineMiss() noexcept;
    void recordWatchdogStall() noexcept;
    void recordThreadException() noexcept;

    // Latest-value gauges. Depth and drop totals mirror a queue's authoritative
    // value (size() / droppedCount()); latency holds the most recent sample.
    void setInputQueueDepth(std::size_t depth) noexcept;
    void setOutputQueueDepth(std::size_t depth) noexcept;
    void setInputDrops(std::uint64_t drops) noexcept;
    void setLogDrops(std::uint64_t drops) noexcept;
    void recordProcessingLatency(Clock::Duration latency) noexcept;

    [[nodiscard]] std::uint64_t signalsProduced() const noexcept;
    [[nodiscard]] std::uint64_t signalsProcessed() const noexcept;
    [[nodiscard]] std::uint64_t inputDrops() const noexcept;
    [[nodiscard]] std::uint64_t logDrops() const noexcept;
    [[nodiscard]] std::uint64_t deadlineMisses() const noexcept;
    [[nodiscard]] std::uint64_t watchdogStalls() const noexcept;
    [[nodiscard]] std::uint64_t threadExceptions() const noexcept;

    [[nodiscard]] std::size_t inputQueueDepth() const noexcept;
    [[nodiscard]] std::size_t outputQueueDepth() const noexcept;
    [[nodiscard]] Clock::Duration lastProcessingLatency() const noexcept;

private:
    std::atomic<std::uint64_t> signalsProduced_{0};
    std::atomic<std::uint64_t> signalsProcessed_{0};
    std::atomic<std::uint64_t> inputDrops_{0};
    std::atomic<std::uint64_t> logDrops_{0};
    std::atomic<std::uint64_t> deadlineMisses_{0};
    std::atomic<std::uint64_t> watchdogStalls_{0};
    std::atomic<std::uint64_t> threadExceptions_{0};

    std::atomic<std::size_t> inputQueueDepth_{0};
    std::atomic<std::size_t> outputQueueDepth_{0};
    std::atomic<Clock::Duration::rep> lastProcessingLatencyTicks_{0};
};

}  // namespace radar
