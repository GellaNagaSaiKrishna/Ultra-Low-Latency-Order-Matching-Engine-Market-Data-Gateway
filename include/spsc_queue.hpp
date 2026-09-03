#pragma once
// =============================================================================
//  spsc_queue.hpp  —  Lock-Free Single-Producer Single-Consumer Ring Buffer
//  Phase 1: Core Memory Infrastructure
//
//  Design:
//    - Power-of-2 capacity for bitmask-based index wrapping (no modulo).
//    - Head/tail are 64-byte padded to prevent false sharing between
//      producer and consumer cache lines.
//    - Acquire/release memory ordering: producer writes data before advancing
//      tail; consumer reads tail before reading data.
//    - Zero heap allocations; storage is a fixed inline array.
//    - NOT safe for multiple producers or multiple consumers.
// =============================================================================

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace ome {

template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "SPSCQueue Capacity must be a power of 2 and >= 2");
    static_assert(std::is_trivially_copyable_v<T>,
                  "SPSCQueue requires trivially copyable T for lock-free safety");

    static constexpr std::size_t kMask = Capacity - 1;

public:
    SPSCQueue() noexcept : head_(0), tail_(0) {}

    // No copy or move.
    SPSCQueue(const SPSCQueue&)            = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // -------------------------------------------------------------------------
    // try_push() — Producer side only.
    // Returns true on success, false if the queue is full.
    // -------------------------------------------------------------------------
    [[nodiscard]] bool try_push(const T& item) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next_tail = (tail + 1) & kMask;

        // Queue is full when next_tail == head.
        if ([[unlikely]] next_tail ==
            head_.load(std::memory_order_acquire)) {
            return false;
        }

        storage_[tail] = item;
        // Release: ensure storage write is visible before tail advances.
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // -------------------------------------------------------------------------
    // try_pop() — Consumer side only.
    // Returns the item if available, std::nullopt if the queue is empty.
    // -------------------------------------------------------------------------
    [[nodiscard]] std::optional<T> try_pop() noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);

        // Queue is empty when head == tail.
        if ([[unlikely]] head ==
            tail_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }

        T item = storage_[head];
        // Release: ensure read completes before head advances.
        head_.store((head + 1) & kMask, std::memory_order_release);
        return item;
    }

    // -------------------------------------------------------------------------
    // Diagnostics (approximate — may be stale across threads).
    // -------------------------------------------------------------------------
    [[nodiscard]] std::size_t size() const noexcept {
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t head = head_.load(std::memory_order_acquire);
        return (tail - head + Capacity) & kMask;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool full() const noexcept {
        const std::size_t next_tail =
            (tail_.load(std::memory_order_acquire) + 1) & kMask;
        return next_tail == head_.load(std::memory_order_acquire);
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return Capacity;
    }

private:
    static constexpr std::size_t kCacheLine = 64;

    // Pad head and tail onto separate cache lines to eliminate false sharing.
    alignas(kCacheLine) std::atomic<std::size_t> head_;
    alignas(kCacheLine) std::atomic<std::size_t> tail_;

    // Ring buffer storage.
    alignas(kCacheLine) T storage_[Capacity];
};

}  // namespace ome
