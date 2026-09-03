// =============================================================================
//  test_spsc_queue.cpp  —  GTest Suite for SPSCQueue<T, Capacity>
// =============================================================================

#include <gtest/gtest.h>
#include "spsc_queue.hpp"

#include <thread>
#include <vector>
#include <numeric>

using namespace ome;

// =============================================================================
//  Single-thread push / pop
// =============================================================================
TEST(SPSCQueue, BasicPushPop) {
    SPSCQueue<int, 4> q;
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.full());

    EXPECT_TRUE(q.try_push(10));
    EXPECT_TRUE(q.try_push(20));
    EXPECT_EQ(q.size(), 2u);

    auto v1 = q.try_pop();
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(*v1, 10);

    auto v2 = q.try_pop();
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(*v2, 20);

    EXPECT_TRUE(q.empty());
}

// =============================================================================
//  Empty queue pop returns nullopt
// =============================================================================
TEST(SPSCQueue, PopEmpty) {
    SPSCQueue<int, 4> q;
    auto v = q.try_pop();
    EXPECT_FALSE(v.has_value());
}

// =============================================================================
//  Full queue push returns false
// =============================================================================
TEST(SPSCQueue, PushFull) {
    SPSCQueue<int, 4> q;  // capacity 4 means 3 usable slots (ring buffer)
    // Fill all usable slots.
    int pushed = 0;
    while (q.try_push(pushed)) ++pushed;
    EXPECT_TRUE(q.full());
    EXPECT_FALSE(q.try_push(99));

    // After popping one, push should succeed again.
    q.try_pop();
    EXPECT_TRUE(q.try_push(99));
}

// =============================================================================
//  Wraparound — verify correct behaviour across the ring boundary
// =============================================================================
TEST(SPSCQueue, Wraparound) {
    SPSCQueue<int, 8> q;
    // Push 6 items.
    for (int i = 0; i < 6; ++i) ASSERT_TRUE(q.try_push(i));
    // Pop 4 items (head advances past midpoint).
    for (int i = 0; i < 4; ++i) q.try_pop();
    // Push 4 more (wraps around the ring).
    for (int i = 10; i < 14; ++i) ASSERT_TRUE(q.try_push(i));

    // Remaining items: 4, 5, 10, 11, 12, 13
    std::vector<int> out;
    while (auto v = q.try_pop()) out.push_back(*v);
    ASSERT_EQ(out.size(), 6u);
    EXPECT_EQ(out[0], 4);
    EXPECT_EQ(out[1], 5);
    EXPECT_EQ(out[2], 10);
    EXPECT_EQ(out[5], 13);
}

// =============================================================================
//  Capacity — verify power-of-2 enforcement via static_assert is met
// =============================================================================
TEST(SPSCQueue, CapacityReported) {
    SPSCQueue<double, 16> q;
    EXPECT_EQ(q.capacity(), 16u);
}

// =============================================================================
//  Multi-threaded SPSC correctness — producer/consumer on separate threads.
//  Verifies ordering and completeness of transfer.
// =============================================================================
TEST(SPSCQueue, MultiThreadedTransfer) {
    static constexpr std::size_t kItems = 100'000;
    SPSCQueue<uint64_t, 1 << 17> q;  // large enough to avoid much blocking

    std::vector<uint64_t> received;
    received.reserve(kItems);

    // Producer thread.
    std::thread producer([&] {
        for (uint64_t i = 0; i < kItems; ++i) {
            while (!q.try_push(i)) { /* spin */ }
        }
    });

    // Consumer thread (main thread acts as consumer here via join).
    std::thread consumer([&] {
        uint64_t count = 0;
        while (count < kItems) {
            if (auto v = q.try_pop()) {
                received.push_back(*v);
                ++count;
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(received.size(), kItems);
    // Verify ordering.
    for (uint64_t i = 0; i < kItems; ++i) {
        EXPECT_EQ(received[i], i) << "mismatch at index " << i;
    }
}
