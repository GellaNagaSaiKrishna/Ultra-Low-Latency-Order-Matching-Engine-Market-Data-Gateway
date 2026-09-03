#pragma once
// =============================================================================
//  intrusive_list.hpp  —  Intrusive Doubly-Linked List
//  Phase 1: Core Memory Infrastructure
//
//  Design:
//    - Nodes embed their own next/prev pointers directly inside the element
//      struct (intrusive), eliminating external wrapper heap allocations.
//    - insert_front(), insert_back(), remove() are all O(1).
//    - No ownership semantics — the pool owns objects; this list just links.
//    - The sentinel head/tail approach eliminates null-checks on insert/remove.
//    - Iteration is forward (begin→end) via range-based for loop support.
// =============================================================================

#include <cassert>
#include <cstddef>
#include <iterator>

namespace ome {

// =============================================================================
//  IntrusiveListNode — embed this (or inherit from it) in your data type.
// =============================================================================
struct IntrusiveListNode {
    IntrusiveListNode* next{nullptr};
    IntrusiveListNode* prev{nullptr};
};

// =============================================================================
//  IntrusiveList<T>
//  T must publicly contain (or inherit) IntrusiveListNode.
//  The accessor is a free function:  IntrusiveListNode* node_of(T*)
//  defaulting to treating T* as IntrusiveListNode* when T derives from it.
// =============================================================================
template <typename T>
class IntrusiveList {
    static_assert(std::is_base_of_v<IntrusiveListNode, T>,
                  "T must derive from IntrusiveListNode for IntrusiveList<T>");

public:
    IntrusiveList() noexcept {
        // Sentinel ring: head <-> tail <-> head
        head_.next = &tail_;
        head_.prev = nullptr;
        tail_.prev = &head_;
        tail_.next = nullptr;
    }

    // No copy or move — nodes hold raw pointers into external memory.
    IntrusiveList(const IntrusiveList&)            = delete;
    IntrusiveList& operator=(const IntrusiveList&) = delete;

    // -------------------------------------------------------------------------
    // insert_back() — append to tail (FIFO queue ordering for price-time priority)
    // -------------------------------------------------------------------------
    void insert_back(T* node) noexcept {
        assert(node != nullptr);
        IntrusiveListNode* n = static_cast<IntrusiveListNode*>(node);
        IntrusiveListNode* prev_node = tail_.prev;

        prev_node->next = n;
        n->prev         = prev_node;
        n->next         = &tail_;
        tail_.prev      = n;

        ++size_;
    }

    // -------------------------------------------------------------------------
    // insert_front() — prepend after head
    // -------------------------------------------------------------------------
    void insert_front(T* node) noexcept {
        assert(node != nullptr);
        IntrusiveListNode* n = static_cast<IntrusiveListNode*>(node);
        IntrusiveListNode* next_node = head_.next;

        head_.next   = n;
        n->prev      = &head_;
        n->next      = next_node;
        next_node->prev = n;

        ++size_;
    }

    // -------------------------------------------------------------------------
    // remove() — unlink node in O(1), node pointers are zeroed after removal
    // -------------------------------------------------------------------------
    void remove(T* node) noexcept {
        assert(node != nullptr);
        assert(size_ > 0 && "remove() on empty list");

        IntrusiveListNode* n = static_cast<IntrusiveListNode*>(node);
        assert(n->prev != nullptr && n->next != nullptr &&
               "Node is not currently linked in a list");

        n->prev->next = n->next;
        n->next->prev = n->prev;
        n->prev       = nullptr;
        n->next       = nullptr;

        --size_;
    }

    // -------------------------------------------------------------------------
    // Accessors
    // -------------------------------------------------------------------------
    [[nodiscard]] T* front() noexcept {
        if (empty()) return nullptr;
        return static_cast<T*>(head_.next);
    }

    [[nodiscard]] const T* front() const noexcept {
        if (empty()) return nullptr;
        return static_cast<const T*>(head_.next);
    }

    [[nodiscard]] T* back() noexcept {
        if (empty()) return nullptr;
        return static_cast<T*>(tail_.prev);
    }

    [[nodiscard]] bool empty()  const noexcept { return size_ == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    // -------------------------------------------------------------------------
    // Forward iterator support for range-based for loops.
    // -------------------------------------------------------------------------
    struct Iterator {
        IntrusiveListNode* cur;
        IntrusiveListNode* sentinel_tail;

        T& operator*()  const noexcept { return *static_cast<T*>(cur); }
        T* operator->() const noexcept { return  static_cast<T*>(cur); }

        Iterator& operator++() noexcept {
            cur = cur->next;
            return *this;
        }

        bool operator==(const Iterator& other) const noexcept {
            return cur == other.cur;
        }
        bool operator!=(const Iterator& other) const noexcept {
            return cur != other.cur;
        }
    };

    Iterator begin() noexcept { return {head_.next, &tail_}; }
    Iterator end()   noexcept { return {&tail_,      &tail_}; }

private:
    IntrusiveListNode head_{};  // sentinel head (not a real node)
    IntrusiveListNode tail_{};  // sentinel tail (not a real node)
    std::size_t       size_{0};
};

}  // namespace ome
