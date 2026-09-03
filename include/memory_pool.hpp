#pragma once
// =============================================================================
//  memory_pool.hpp  —  Static Fixed-Capacity Slab Allocator
//  Phase 1: Core Memory Infrastructure
//
//  Design:
//    - Pre-allocates `Capacity` slots of type T in a contiguous aligned array.
//    - A free-list stack provides O(1) acquire() and release().
//    - Zero heap allocations after construction (zero malloc on hot path).
//    - Slots are cache-line aligned (64 bytes) to prevent false sharing.
//    - NOT thread-safe by design; intended for single-threaded matching core.
// =============================================================================

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ome {  // order matching engine

template <typename T, std::size_t Capacity>
class MemoryPool {
    static_assert(Capacity > 0, "MemoryPool capacity must be > 0");
    static_assert(std::is_default_constructible_v<T>,
                  "MemoryPool requires default-constructible T");

public:
    // -------------------------------------------------------------------------
    // Slot: raw storage with alignment matching max(T, cache-line).
    // -------------------------------------------------------------------------
    static constexpr std::size_t kCacheLine = 64;
    static constexpr std::size_t kSlotAlign =
        alignof(T) > kCacheLine ? alignof(T) : kCacheLine;

    struct alignas(kSlotAlign) Slot {
        alignas(alignof(T)) std::byte storage[sizeof(T)];
    };

    // -------------------------------------------------------------------------
    // Construction: builds the free-list in reverse so index 0 is served first.
    // -------------------------------------------------------------------------
    MemoryPool() noexcept {
        for (std::size_t i = 0; i < Capacity; ++i) {
            free_stack_[i] = &slots_[Capacity - 1 - i];
        }
        free_top_ = Capacity;
    }

    // No copy, no move — pool owns its memory.
    MemoryPool(const MemoryPool&)            = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&)                 = delete;
    MemoryPool& operator=(MemoryPool&&)      = delete;

    ~MemoryPool() noexcept = default;

    // -------------------------------------------------------------------------
    // acquire() — returns a pointer to an uninitialised T slot.
    // Caller must placement-new the object. Returns nullptr if pool exhausted.
    // -------------------------------------------------------------------------
    [[nodiscard]] T* acquire() noexcept {
        if ([[unlikely]] free_top_ == 0) {
            return nullptr;  // pool exhausted
        }
        Slot* slot = free_stack_[--free_top_];
        return reinterpret_cast<T*>(slot->storage);
    }

    // -------------------------------------------------------------------------
    // release() — returns a slot back to the free-list.
    // Caller is responsible for calling the destructor before releasing.
    // -------------------------------------------------------------------------
    void release(T* ptr) noexcept {
        assert(ptr != nullptr);
        assert(free_top_ < Capacity && "MemoryPool double-release detected");
        // Compute which slot this pointer belongs to.
        auto* slot = reinterpret_cast<Slot*>(
            reinterpret_cast<std::byte*>(ptr));
        assert(owns(ptr) && "Pointer not owned by this MemoryPool");
        free_stack_[free_top_++] = slot;
    }

    // -------------------------------------------------------------------------
    // Convenience: construct an object in-pool and return its pointer.
    // -------------------------------------------------------------------------
    template <typename... Args>
    [[nodiscard]] T* construct(Args&&... args) noexcept(
        std::is_nothrow_constructible_v<T, Args...>) {
        T* p = acquire();
        if ([[unlikely]] p == nullptr) return nullptr;
        return new (p) T(std::forward<Args>(args)...);
    }

    // -------------------------------------------------------------------------
    // destroy() — calls ~T() and releases the slot.
    // -------------------------------------------------------------------------
    void destroy(T* ptr) noexcept {
        ptr->~T();
        release(ptr);
    }

    // -------------------------------------------------------------------------
    // Diagnostics
    // -------------------------------------------------------------------------
    [[nodiscard]] std::size_t available() const noexcept { return free_top_; }
    [[nodiscard]] std::size_t capacity()  const noexcept { return Capacity;   }
    [[nodiscard]] bool        empty()     const noexcept { return free_top_ == 0; }
    [[nodiscard]] bool        full()      const noexcept { return free_top_ == Capacity; }

    // -------------------------------------------------------------------------
    // owns() — true if ptr falls within this pool's storage.
    // -------------------------------------------------------------------------
    [[nodiscard]] bool owns(const T* ptr) const noexcept {
        const auto* raw = reinterpret_cast<const std::byte*>(ptr);
        const auto* begin = reinterpret_cast<const std::byte*>(slots_.data());
        const auto* end   = begin + sizeof(slots_);
        return raw >= begin && raw < end;
    }

private:
    std::array<Slot, Capacity>   slots_{};
    std::array<Slot*, Capacity>  free_stack_{};
    std::size_t                  free_top_{0};
};

}  // namespace ome
