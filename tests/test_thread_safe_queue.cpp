// Unit tests for ThreadSafeQueue<T>: push/pop FIFO,
// bounded capacity, the three overflow policies and their drop accounting, the
// close-then-drain (end-of-stream) semantics, and waitUntilEmpty. Concurrent
// behaviour is exercised in the "synchronization" suite; these tests are
// single-threaded and fully deterministic, except the one waitUntilEmpty case
// that needs a draining thread.

#include "radar/managed_thread.hpp"
#include "radar/thread_safe_queue.hpp"
#include "test_framework.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>

using radar::ManagedThread;
using radar::OverflowPolicy;
using radar::ThreadSafeQueue;

RADAR_TEST(queue, zero_capacity_throws) {
    bool threw = false;
    try {
        ThreadSafeQueue<int> queue(0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    RADAR_EXPECT(threw);
}

RADAR_TEST(queue, push_pop_is_fifo) {
    ThreadSafeQueue<int> queue(8);
    RADAR_ASSERT(queue.push(1));
    RADAR_ASSERT(queue.push(2));
    RADAR_ASSERT(queue.push(3));
    RADAR_EXPECT_EQ(queue.size(), std::size_t{3});

    RADAR_EXPECT_EQ(queue.tryPop().value(), 1);
    RADAR_EXPECT_EQ(queue.tryPop().value(), 2);
    RADAR_EXPECT_EQ(queue.tryPop().value(), 3);
    RADAR_EXPECT(!queue.tryPop().has_value());
}

RADAR_TEST(queue, try_pop_on_empty_returns_nullopt) {
    ThreadSafeQueue<int> queue(4);
    RADAR_EXPECT(!queue.tryPop().has_value());
}

RADAR_TEST(queue, block_policy_try_push_rejects_when_full) {
    // tryPush never blocks: under Block policy a full queue rejects the item and
    // counts it as a drop.
    ThreadSafeQueue<int> queue(2, OverflowPolicy::Block);
    RADAR_ASSERT(queue.tryPush(1));
    RADAR_ASSERT(queue.tryPush(2));
    RADAR_EXPECT(!queue.tryPush(3));
    RADAR_EXPECT_EQ(queue.size(), std::size_t{2});
    RADAR_EXPECT_EQ(queue.droppedCount(), std::uint64_t{1});
}

RADAR_TEST(queue, drop_newest_keeps_existing_items) {
    ThreadSafeQueue<int> queue(2, OverflowPolicy::DropNewest);
    RADAR_ASSERT(queue.push(1));
    RADAR_ASSERT(queue.push(2));
    RADAR_EXPECT(!queue.push(3));  // incoming item discarded
    RADAR_EXPECT_EQ(queue.droppedCount(), std::uint64_t{1});
    RADAR_EXPECT_EQ(queue.tryPop().value(), 1);
    RADAR_EXPECT_EQ(queue.tryPop().value(), 2);
    RADAR_EXPECT(!queue.tryPop().has_value());
}

RADAR_TEST(queue, drop_oldest_evicts_front) {
    ThreadSafeQueue<int> queue(2, OverflowPolicy::DropOldest);
    RADAR_ASSERT(queue.push(1));
    RADAR_ASSERT(queue.push(2));
    RADAR_ASSERT(queue.push(3));  // evicts 1, stores 3
    RADAR_EXPECT_EQ(queue.droppedCount(), std::uint64_t{1});
    RADAR_EXPECT_EQ(queue.size(), std::size_t{2});
    RADAR_EXPECT_EQ(queue.tryPop().value(), 2);
    RADAR_EXPECT_EQ(queue.tryPop().value(), 3);
}

RADAR_TEST(queue, close_rejects_pushes_without_counting_drops) {
    ThreadSafeQueue<int> queue(4, OverflowPolicy::Block);
    RADAR_ASSERT(queue.push(1));
    queue.close();
    RADAR_EXPECT(!queue.push(2));  // closed: stores nothing, returns false
    RADAR_EXPECT(!queue.tryPush(3));
    // A push failing because the queue is closed is shutdown, not an overflow drop.
    RADAR_EXPECT_EQ(queue.droppedCount(), std::uint64_t{0});
}

RADAR_TEST(queue, close_drains_buffered_items_then_end_of_stream) {
    ThreadSafeQueue<int> queue(4);
    RADAR_ASSERT(queue.push(10));
    RADAR_ASSERT(queue.push(20));
    queue.close();  // closing a non-empty queue must not discard buffered items

    RADAR_EXPECT_EQ(queue.tryPop().value(), 10);
    RADAR_EXPECT_EQ(queue.tryPop().value(), 20);
    // Drained and closed: waitAndPop returns end-of-stream without blocking.
    RADAR_EXPECT(!queue.waitAndPop().has_value());
    RADAR_EXPECT(!queue.tryPop().has_value());
}

RADAR_TEST(queue, wait_until_empty_returns_immediately_when_empty) {
    ThreadSafeQueue<int> queue(4);
    queue.waitUntilEmpty();  // already empty: must return at once
    RADAR_EXPECT_EQ(queue.size(), std::size_t{0});
}

RADAR_TEST(queue, wait_until_empty_blocks_until_drained) {
    ThreadSafeQueue<int> queue(8);
    RADAR_ASSERT(queue.push(1));
    RADAR_ASSERT(queue.push(2));
    RADAR_ASSERT(queue.push(3));

    // A consumer drains exactly the three buffered items; the items are already
    // present, so each waitAndPop returns without blocking and the thread exits.
    ManagedThread consumer([&queue] {
        for (int i = 0; i < 3; ++i) {
            (void)queue.waitAndPop();
        }
    });

    queue.waitUntilEmpty();  // returns once the consumer has drained the queue
    consumer.join();
    RADAR_EXPECT_EQ(queue.size(), std::size_t{0});
}
