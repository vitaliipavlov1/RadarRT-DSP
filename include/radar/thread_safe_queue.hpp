#pragma once

#include "radar/pi_mutex.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <utility>

namespace radar {

// Behaviour when push() is called on a full queue.
enum class OverflowPolicy {
    Block,       // wait until space is available (or the queue is closed)
    DropNewest,  // discard the incoming item
    DropOldest,  // evict the oldest item, then store the incoming one
};

// Bounded, thread-safe, single-ended queue used to hand data between subsystems.
//
// Synchronization is centralized here (ARCHITECTURE.md): producers and consumers
// never lock each other directly. Blocking is provided by two condition variables
// (not-empty / not-full); there is no busy waiting. The lock is a PiMutex
// (priority-inheritance): the producer, DSP workers and logger contend for it while
// running at different realtime priorities, so a plain mutex could cause priority
// inversion (ARCHITECTURE.md "Realtime Strategy", D2). std::condition_variable_any is
// used precisely because the lock type is custom rather than a std::mutex.
//
// Non-copyable and non-movable: it owns synchronization primitives and is shared
// by reference.
//
// The queue counts its own overflow drops (a DropOldest eviction, or a
// DropNewest/Block rejection of the incoming item on a full queue) in an
// internal atomic, readable via droppedCount(). It never counts a push()
// failing because the queue is closed (that is shutdown, not a drop). This
// keeps the queue fully self-contained and decoupled from Metrics: it exposes
// a fact about itself, the same way size() does, rather than calling out to
// external code. The caller that owns both the queue and a Metrics instance
// samples droppedCount() (alongside size()) to satisfy "drops counted in
// Metrics, never discarded silently" (ARCHITECTURE.md D5).
template <class T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(std::size_t capacity, OverflowPolicy policy = OverflowPolicy::Block)
        : capacity_(capacity), policy_(policy) {
        if (capacity_ == 0) {
            throw std::invalid_argument("ThreadSafeQueue capacity must be greater than zero");
        }
    }

    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue(ThreadSafeQueue&&) = delete;
    ThreadSafeQueue& operator=(ThreadSafeQueue&&) = delete;
    ~ThreadSafeQueue() = default;

    // Stores `value`, applying the overflow policy when the queue is full. With
    // OverflowPolicy::Block this waits until space is available or the queue is
    // closed. Returns true if the item was stored; false if the queue is closed
    // or the item was dropped (DropNewest on a full queue).
    [[nodiscard]] bool push(T value) {
        std::unique_lock<PiMutex> lock(mutex_);
        if (policy_ == OverflowPolicy::Block) {
            not_full_.wait(lock, [this] { return !full() || closed_; });
        }
        return enqueueLocked(std::move(value), lock);
    }

    // Non-blocking push. Never waits, regardless of policy. Returns true if the
    // item was stored; false if the queue is closed, or full under Block /
    // DropNewest.
    [[nodiscard]] bool tryPush(T value) {
        std::unique_lock<PiMutex> lock(mutex_);
        return enqueueLocked(std::move(value), lock);
    }

    // Waits for an item and returns it. Returns std::nullopt only when the queue
    // is closed and fully drained (end-of-stream).
    [[nodiscard]] std::optional<T> waitAndPop() {
        std::unique_lock<PiMutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return !queue_.empty() || closed_; });
        if (queue_.empty()) {
            return std::nullopt;
        }
        return popLockedAndNotify(lock);
    }

    // Non-blocking pop. Returns std::nullopt if the queue is currently empty.
    [[nodiscard]] std::optional<T> tryPop() {
        std::unique_lock<PiMutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        return popLockedAndNotify(lock);
    }

    // Idempotently closes the queue and wakes every waiter. After close(),
    // push()/tryPush() store nothing and return false, while waitAndPop()/tryPop()
    // keep returning buffered items until the queue is empty.
    void close() noexcept {
        {
            std::lock_guard<PiMutex> lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    // Blocks until the queue is empty.
    void waitUntilEmpty() const {
        std::unique_lock<PiMutex> lock(mutex_);
        not_full_.wait(lock, [this] { return queue_.empty(); });
    }

    // Approximate snapshot of the current depth, for observability (Metrics
    // "per-queue depth (approximate)"). Briefly locked but not synchronized
    // with any particular push()/pop(): by the time the caller reads the
    // result it may already be stale, hence "approximate".
    [[nodiscard]] std::size_t size() const {
        std::lock_guard<PiMutex> lock(mutex_);
        return queue_.size();
    }

    // Total number of items discarded by the overflow policy over the queue's
    // lifetime (a DropOldest eviction, or a DropNewest/Block rejection of the
    // incoming item). Lock-free acquire read, pairing with the release increment
    // inside enqueueLocked (the project-wide ordering rule); intended to be
    // sampled by whoever mirrors it into Metrics (Metrics "input drops" /
    // "log-record drops").
    [[nodiscard]] std::uint64_t droppedCount() const noexcept {
        return droppedCount_.load(std::memory_order_acquire);
    }

private:
    [[nodiscard]] bool full() const { return queue_.size() >= capacity_; }

    // Precondition: `lock` is held. Stores `value` unless the queue is closed or
    // (for a full queue) the policy forbids it; unlocks before notifying so the
    // critical section stays minimal. A drop is recorded with a release fetch_add
    // (no external call, so no risk of recursion, deadlock or escaping
    // exceptions); the increment is held under the lock because it is a trivial
    // non-blocking operation.
    bool enqueueLocked(T&& value, std::unique_lock<PiMutex>& lock) {
        if (closed_) {
            return false;
        }
        if (full()) {
            if (policy_ == OverflowPolicy::DropOldest) {
                queue_.pop();
                droppedCount_.fetch_add(1, std::memory_order_release);
            } else {
                droppedCount_.fetch_add(1, std::memory_order_release);
                return false;  // Block (non-blocking attempt) or DropNewest
            }
        }
        queue_.push(std::move(value));
        lock.unlock();
        not_empty_.notify_one();
        return true;
    }

    // Precondition: `lock` is held and the queue is non-empty. Moves out the front
    // item, unlocks, and wakes the not-full waiters.
    std::optional<T> popLockedAndNotify(std::unique_lock<PiMutex>& lock) {
        std::optional<T> value(std::move(queue_.front()));
        queue_.pop();
        lock.unlock();
        // notify_all, not notify_one: producers (predicate !full() || closed_) and
        // waitUntilEmpty (predicate empty()) share not_full_. Waking a single
        // waiter could wake a waitUntilEmpty waiter that immediately re-sleeps
        // while a producer with a now-free slot is left blocked (lost wakeup).
        not_full_.notify_all();
        return value;
    }

    mutable PiMutex mutex_;
    mutable std::condition_variable_any not_empty_;
    mutable std::condition_variable_any not_full_;
    std::queue<T> queue_;
    std::size_t capacity_;
    OverflowPolicy policy_;
    std::atomic<std::uint64_t> droppedCount_{0};
    bool closed_{false};
};

}  // namespace radar
