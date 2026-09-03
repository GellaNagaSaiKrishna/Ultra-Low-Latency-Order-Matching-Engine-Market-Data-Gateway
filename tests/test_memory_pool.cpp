// =============================================================================
//  test_memory_pool.cpp  —  GTest Suite for MemoryPool<T, Capacity>
// =============================================================================

#include <gtest/gtest.h>
#include "memory_pool.hpp"

using namespace ome;

// A simple test type — track construct/destruct calls.
struct Widget {
    int value{0};
    static int live_count;
    Widget() { ++live_count; }
    explicit Widget(int v) : value(v) { ++live_count; }
    ~Widget() { --live_count; }
};
int Widget::live_count = 0;

// =============================================================================
//  Basic acquire / release
// =============================================================================
TEST(MemoryPool, AcquireAndRelease) {
    MemoryPool<Widget, 4> pool;
    EXPECT_EQ(pool.capacity(),  4u);
    EXPECT_EQ(pool.available(), 4u);
    EXPECT_TRUE(pool.full());

    Widget* w = pool.acquire();
    ASSERT_NE(w, nullptr);
    EXPECT_EQ(pool.available(), 3u);
    EXPECT_FALSE(pool.full());
    EXPECT_FALSE(pool.empty());

    pool.release(w);
    EXPECT_EQ(pool.available(), 4u);
    EXPECT_TRUE(pool.full());
}

// =============================================================================
//  construct / destroy — verifies RAII lifecycle
// =============================================================================
TEST(MemoryPool, ConstructAndDestroy) {
    Widget::live_count = 0;
    {
        MemoryPool<Widget, 8> pool;
        Widget* a = pool.construct(42);
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(a->value, 42);
        EXPECT_EQ(Widget::live_count, 1);

        Widget* b = pool.construct(99);
        EXPECT_EQ(Widget::live_count, 2);

        pool.destroy(a);
        EXPECT_EQ(Widget::live_count, 1);
        pool.destroy(b);
        EXPECT_EQ(Widget::live_count, 0);
    }
    EXPECT_EQ(Widget::live_count, 0);
}

// =============================================================================
//  Pool exhaustion — acquire returns nullptr when full
// =============================================================================
TEST(MemoryPool, Exhaustion) {
    MemoryPool<Widget, 2> pool;
    Widget* a = pool.construct();
    Widget* b = pool.construct();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(pool.empty());

    Widget* c = pool.acquire();
    EXPECT_EQ(c, nullptr);  // pool exhausted

    pool.destroy(a);
    EXPECT_FALSE(pool.empty());
    Widget* d = pool.acquire();
    EXPECT_NE(d, nullptr);  // slot recycled
    pool.release(d);
    pool.destroy(b);
}

// =============================================================================
//  owns() — pointer ownership detection
// =============================================================================
TEST(MemoryPool, Owns) {
    MemoryPool<Widget, 4> pool;
    Widget* w = pool.construct();
    EXPECT_TRUE(pool.owns(w));

    Widget external{};
    EXPECT_FALSE(pool.owns(&external));

    pool.destroy(w);
}

// =============================================================================
//  Cycle stress — acquire/release cycle many times
// =============================================================================
TEST(MemoryPool, CycleStress) {
    MemoryPool<Widget, 64> pool;
    Widget::live_count = 0;

    for (int iter = 0; iter < 10000; ++iter) {
        Widget* w = pool.construct(iter);
        ASSERT_NE(w, nullptr);
        EXPECT_EQ(w->value, iter);
        pool.destroy(w);
    }
    EXPECT_EQ(Widget::live_count, 0);
    EXPECT_TRUE(pool.full());
}

// =============================================================================
//  Alignment — all slots must be properly aligned
// =============================================================================
TEST(MemoryPool, Alignment) {
    MemoryPool<Widget, 8> pool;
    std::vector<Widget*> ptrs;
    while (!pool.empty()) {
        ptrs.push_back(pool.acquire());
    }
    for (Widget* p : ptrs) {
        EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % alignof(Widget), 0u);
    }
    for (Widget* p : ptrs) pool.release(p);
}
