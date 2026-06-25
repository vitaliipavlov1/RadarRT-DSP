#include "radar/shutdown_token.hpp"

namespace radar {

void ShutdownToken::requestShutdown() noexcept {
    {
        // Set the flag under the lock so a thread about to wait cannot miss the
        // notification (lost-wakeup safety); the flag stays atomic for lock-free
        // observation via shutdownRequested().
        std::lock_guard<std::mutex> lock(mutex_);
        requested_.store(true, std::memory_order_release);
    }
    cv_.notify_all();
}

bool ShutdownToken::shutdownRequested() const noexcept {
    return requested_.load(std::memory_order_acquire);
}

}  // namespace radar
