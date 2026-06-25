// Unit tests for ShutdownToken: the flag state,
// idempotent requests, and the wakeable wait. The "not requested" timeout path
// uses a zero-length wait so the test never sleeps; the wake-up path blocks a
// worker thread and proves requestShutdown() releases it promptly.

#include "radar/managed_thread.hpp"
#include "radar/shutdown_token.hpp"
#include "test_framework.hpp"

#include <atomic>
#include <chrono>

using radar::ManagedThread;
using radar::ShutdownToken;

RADAR_TEST(shutdown_token, initially_not_requested) {
    ShutdownToken token;
    RADAR_EXPECT(!token.shutdownRequested());
    // Zero-length wait: checks the predicate once and times out immediately.
    RADAR_EXPECT(!token.waitFor(std::chrono::milliseconds(0)));
}

RADAR_TEST(shutdown_token, request_sets_flag) {
    ShutdownToken token;
    token.requestShutdown();
    RADAR_EXPECT(token.shutdownRequested());
    RADAR_EXPECT(token.waitFor(std::chrono::milliseconds(0)));  // already requested
}

RADAR_TEST(shutdown_token, request_is_idempotent) {
    ShutdownToken token;
    token.requestShutdown();
    token.requestShutdown();
    RADAR_EXPECT(token.shutdownRequested());
}

RADAR_TEST(shutdown_token, blocked_wait_is_woken_by_request) {
    ShutdownToken token;
    std::atomic<bool> woken{false};

    // The waiter blocks on a long timeout; only requestShutdown() should release
    // it. A true result therefore means it was woken, not that it timed out.
    ManagedThread waiter([&token, &woken] {
        const bool result = token.waitFor(std::chrono::seconds(10));
        woken.store(result, std::memory_order_release);
    });

    // Setting the flag under the token's mutex rules out a lost wake-up: whether
    // the waiter is already blocked or about to block, it observes the request.
    token.requestShutdown();
    waiter.join();

    RADAR_EXPECT(woken.load(std::memory_order_acquire));
}
