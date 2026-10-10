#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace radar {

// Cooperative shutdown signal for threads that block OUTSIDE a queue: the
// producer's periodic sleep, the watchdog's periodic sleep, the signal-handler
// wait. It offers a wakeable sleep, so a sleeping thread is released the instant
// shutdown is requested instead of finishing its full timeout — no busy waiting
// and no unstoppable sleep. Queue waits are handled separately by
// ThreadSafeQueue::close() (see ARCHITECTURE.md D4).
//
// Owns synchronization primitives, therefore non-copyable and non-movable.
class ShutdownToken {
public:
    ShutdownToken() = default;
    ~ShutdownToken() = default;

    ShutdownToken(const ShutdownToken&) = delete;
    ShutdownToken& operator=(const ShutdownToken&) = delete;
    ShutdownToken(ShutdownToken&&) = delete;
    ShutdownToken& operator=(ShutdownToken&&) = delete;

    void requestShutdown() noexcept;

    [[nodiscard]] bool shutdownRequested() const noexcept;

    // Blocks up to `timeout`. Returns true if shutdown was requested (already, or
    // before the timeout elapsed); false if the timeout elapsed first.
    template <class Rep, class Period>
    [[nodiscard]] bool waitFor(const std::chrono::duration<Rep, Period>& timeout) const {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout,
                            [this] { return requested_.load(std::memory_order_acquire); });
    }

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    std::atomic<bool> requested_{false};
};

}  // namespace radar
