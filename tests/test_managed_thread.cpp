// Unit tests for ManagedThread: join-on-destruction,
// idempotent join, and move semantics (the move constructor transfers ownership;
// move assignment joins the previously owned thread first). Threads that must stay
// alive across an observation block on a ShutdownToken rather than on a sleep, so
// the tests are deterministic.

#include "radar/managed_thread.hpp"
#include "radar/shutdown_token.hpp"
#include "test_framework.hpp"

#include <atomic>
#include <chrono>
#include <utility>

using radar::ManagedThread;
using radar::ShutdownToken;

RADAR_TEST(managed_thread, joins_on_destruction) {
    std::atomic<bool> ran{false};
    {
        ManagedThread thread([&ran] { ran.store(true, std::memory_order_release); });
    }  // destructor joins: the thread has certainly finished here
    RADAR_EXPECT(ran.load(std::memory_order_acquire));
}

RADAR_TEST(managed_thread, join_is_idempotent) {
    std::atomic<int> count{0};
    ManagedThread thread([&count] { count.fetch_add(1, std::memory_order_acq_rel); });
    thread.join();
    RADAR_EXPECT(!thread.joinable());
    thread.join();  // second join is a no-op, must not terminate
    RADAR_EXPECT_EQ(count.load(std::memory_order_acquire), 1);
}

RADAR_TEST(managed_thread, move_construction_transfers_ownership) {
    ShutdownToken token;
    ManagedThread source([&token] { (void)token.waitFor(std::chrono::seconds(10)); });
    RADAR_EXPECT(source.joinable());

    ManagedThread sink(std::move(source));
    RADAR_EXPECT(!source.joinable());  // ownership moved out
    RADAR_EXPECT(sink.joinable());

    token.requestShutdown();
    sink.join();
    RADAR_EXPECT(!sink.joinable());
}

RADAR_TEST(managed_thread, move_assignment_joins_previous_thread) {
    ShutdownToken firstToken;
    ShutdownToken secondToken;
    std::atomic<bool> firstFinished{false};
    std::atomic<bool> secondFinished{false};

    ManagedThread first([&firstToken, &firstFinished] {
        (void)firstToken.waitFor(std::chrono::seconds(10));
        firstFinished.store(true, std::memory_order_release);
    });
    ManagedThread second([&secondToken, &secondFinished] {
        (void)secondToken.waitFor(std::chrono::seconds(10));
        secondFinished.store(true, std::memory_order_release);
    });

    // Release the first thread, then move-assign over it: the assignment must join
    // the thread `first` currently owns before adopting `second`'s thread.
    firstToken.requestShutdown();
    first = std::move(second);
    RADAR_EXPECT(firstFinished.load(std::memory_order_acquire));  // previous thread joined

    secondToken.requestShutdown();
    first.join();  // join the adopted thread
    RADAR_EXPECT(secondFinished.load(std::memory_order_acquire));
}

RADAR_TEST(managed_thread, self_move_assignment_is_safe) {
    std::atomic<bool> ran{false};
    ManagedThread thread([&ran] { ran.store(true, std::memory_order_release); });

    // Route through a reference so the self-assignment is not a syntactic
    // self-move (which compilers diagnose); operator= guards `this != &other`.
    ManagedThread& alias = thread;
    thread = std::move(alias);

    thread.join();
    RADAR_EXPECT(ran.load(std::memory_order_acquire));
}
