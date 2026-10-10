// Concurrency tests for the queue-centred synchronization model: graceful shutdown
// and ordered drain (no lost items), and thread synchronization. These exercise
// the real cross-thread paths of
// ThreadSafeQueue: many producers and consumers exchanging items with no loss or
// duplication, the close-then-drain end-of-stream protocol that lets consumers
// exit, and a producer blocked in push() being woken by close(). Threads
// coordinate through the queue's own blocking operations, never through sleeps.

#include "radar/managed_thread.hpp"
#include "radar/thread_safe_queue.hpp"
#include "test_framework.hpp"

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

using radar::ManagedThread;
using radar::OverflowPolicy;
using radar::ThreadSafeQueue;

RADAR_TEST(synchronization, mpmc_drain_loses_no_items) {
    constexpr int kProducers = 4;
    constexpr int kConsumers = 4;
    constexpr int kPerProducer = 2000;
    constexpr int kTotal = kProducers * kPerProducer;

    // A small bounded Block queue forces real backpressure between the pools.
    ThreadSafeQueue<int> queue(32, OverflowPolicy::Block);

    std::atomic<long long> sum{0};
    std::atomic<int> popped{0};
    std::atomic<bool> pushFailed{false};

    std::vector<ManagedThread> consumers;
    consumers.reserve(kConsumers);
    for (int c = 0; c < kConsumers; ++c) {
        consumers.emplace_back([&queue, &sum, &popped] {
            // Exits only on end-of-stream (queue closed and drained).
            while (std::optional<int> item = queue.waitAndPop()) {
                sum.fetch_add(*item, std::memory_order_relaxed);
                popped.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::vector<ManagedThread> producers;
    producers.reserve(kProducers);
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&queue, &pushFailed, p] {
            for (int i = 0; i < kPerProducer; ++i) {
                const int value = p * kPerProducer + i;
                if (!queue.push(value)) {
                    pushFailed.store(true, std::memory_order_release);  // queue closed early
                    return;
                }
            }
        });
    }

    for (ManagedThread& producer : producers) {
        producer.join();
    }
    queue.close();  // staged: producers done -> close -> consumers drain and exit
    for (ManagedThread& consumer : consumers) {
        consumer.join();
    }

    // Every produced value 0 .. kTotal-1 must have been consumed exactly once.
    long long expectedSum = 0;
    for (int v = 0; v < kTotal; ++v) {
        expectedSum += v;
    }

    RADAR_EXPECT(!pushFailed.load(std::memory_order_acquire));
    RADAR_EXPECT_EQ(popped.load(std::memory_order_acquire), kTotal);
    RADAR_EXPECT_EQ(sum.load(std::memory_order_acquire), expectedSum);
    RADAR_EXPECT_EQ(queue.size(), std::size_t{0});
}

RADAR_TEST(synchronization, blocked_push_is_woken_by_close) {
    ThreadSafeQueue<int> queue(1, OverflowPolicy::Block);
    RADAR_ASSERT(queue.push(1));  // fill to capacity

    std::atomic<bool> pushReturned{false};
    std::atomic<bool> secondPushResult{true};

    ManagedThread producer([&queue, &pushReturned, &secondPushResult] {
        // Blocks because the queue is full; close() must wake it and it must then
        // observe the closed queue and return false.
        const bool stored = queue.push(2);
        secondPushResult.store(stored, std::memory_order_release);
        pushReturned.store(true, std::memory_order_release);
    });

    queue.close();
    producer.join();

    RADAR_EXPECT(pushReturned.load(std::memory_order_acquire));
    RADAR_EXPECT(!secondPushResult.load(std::memory_order_acquire));

    // close() must not have discarded the buffered item.
    std::optional<int> buffered = queue.tryPop();
    RADAR_ASSERT(buffered.has_value());
    RADAR_EXPECT_EQ(buffered.value(), 1);
}
