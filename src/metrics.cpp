#include "radar/metrics.hpp"

namespace radar {

// Project-wide ordering rule (ARCHITECTURE.md "Synchronization Strategy"):
// release on write, acquire on read — the same idiom as Heartbeat,
// ShutdownToken and ManualClock. Applied uniformly here; no relaxed access.

void Metrics::recordSignalProduced() noexcept {
    signalsProduced_.fetch_add(1, std::memory_order_release);
}

void Metrics::recordSignalProcessed() noexcept {
    signalsProcessed_.fetch_add(1, std::memory_order_release);
}

void Metrics::recordDeadlineMiss() noexcept {
    deadlineMisses_.fetch_add(1, std::memory_order_release);
}

void Metrics::recordWatchdogStall() noexcept {
    watchdogStalls_.fetch_add(1, std::memory_order_release);
}

void Metrics::recordThreadException() noexcept {
    threadExceptions_.fetch_add(1, std::memory_order_release);
}

void Metrics::setInputQueueDepth(std::size_t depth) noexcept {
    inputQueueDepth_.store(depth, std::memory_order_release);
}

void Metrics::setOutputQueueDepth(std::size_t depth) noexcept {
    outputQueueDepth_.store(depth, std::memory_order_release);
}

void Metrics::setInputDrops(std::uint64_t drops) noexcept {
    inputDrops_.store(drops, std::memory_order_release);
}

void Metrics::setLogDrops(std::uint64_t drops) noexcept {
    logDrops_.store(drops, std::memory_order_release);
}

void Metrics::recordProcessingLatency(Clock::Duration latency) noexcept {
    lastProcessingLatencyTicks_.store(latency.count(), std::memory_order_release);
}

std::uint64_t Metrics::signalsProduced() const noexcept {
    return signalsProduced_.load(std::memory_order_acquire);
}

std::uint64_t Metrics::signalsProcessed() const noexcept {
    return signalsProcessed_.load(std::memory_order_acquire);
}

std::uint64_t Metrics::inputDrops() const noexcept {
    return inputDrops_.load(std::memory_order_acquire);
}

std::uint64_t Metrics::logDrops() const noexcept {
    return logDrops_.load(std::memory_order_acquire);
}

std::uint64_t Metrics::deadlineMisses() const noexcept {
    return deadlineMisses_.load(std::memory_order_acquire);
}

std::uint64_t Metrics::watchdogStalls() const noexcept {
    return watchdogStalls_.load(std::memory_order_acquire);
}

std::uint64_t Metrics::threadExceptions() const noexcept {
    return threadExceptions_.load(std::memory_order_acquire);
}

std::size_t Metrics::inputQueueDepth() const noexcept {
    return inputQueueDepth_.load(std::memory_order_acquire);
}

std::size_t Metrics::outputQueueDepth() const noexcept {
    return outputQueueDepth_.load(std::memory_order_acquire);
}

Clock::Duration Metrics::lastProcessingLatency() const noexcept {
    return Clock::Duration{lastProcessingLatencyTicks_.load(std::memory_order_acquire)};
}

}  // namespace radar
