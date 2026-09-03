#pragma once
// =============================================================================
//  types.hpp  —  Core Domain Types (POD Layout Structs)
//  Phase 2: Matching Engine & Order Book Logic
//
//  All structs are Plain-Old-Data to allow zero-copy memcpy and direct
//  reinterpret_cast from wire buffers. No virtual functions, no heap pointers.
// =============================================================================

#include <cstdint>
#include <cstring>
#include "intrusive_list.hpp"

namespace ome {

// =============================================================================
//  Enumerations
// =============================================================================

enum class Side : uint8_t {
    Buy  = 0,
    Sell = 1,
};

enum class OrderAction : uint8_t {
    Add    = 0,
    Cancel = 1,
    Modify = 2,
    Execute = 3,
};

enum class OrderStatus : uint8_t {
    Active        = 0,
    PartiallyFilled = 1,
    Filled        = 2,
    Cancelled     = 3,
};

// =============================================================================
//  Price / Quantity types
//  Prices are represented as integer ticks (e.g. price * 100 for 2 d.p.)
//  to avoid floating point non-determinism on the hot path.
// =============================================================================
using Price    = int64_t;   // signed: negative tick = invalid
using Quantity = uint64_t;
using OrderId  = uint64_t;
using Nanos    = uint64_t;  // nanosecond timestamp

static constexpr Price    kInvalidPrice = -1;
static constexpr OrderId  kInvalidOrderId = 0;

// =============================================================================
//  Order — the fundamental unit processed by the matching engine.
//
//  Inherits IntrusiveListNode so orders can be stored in an IntrusiveList
//  at each price level without any additional heap allocation.
// =============================================================================
struct alignas(64) Order : public IntrusiveListNode {
    OrderId   id{kInvalidOrderId};
    Side      side{Side::Buy};
    OrderStatus status{OrderStatus::Active};
    uint8_t   _pad0[6]{};       // explicit padding to control layout

    Price     price{kInvalidPrice};
    Quantity  quantity{0};
    Quantity  filled_quantity{0};

    Nanos     timestamp{0};     // time of arrival (rdtsc cycles at entry)
    Nanos     entry_nanos{0};   // wall-clock entry (for latency reporting)
};

static_assert(sizeof(Order) >= 64, "Order should span at least one cache line");

// =============================================================================
//  LimitLevel — one price point in the order book.
//
//  Holds a doubly-linked intrusive list of all orders resting at this price.
//  Stored in a flat array indexed by price tick.
// =============================================================================
struct alignas(64) LimitLevel {
    Price     price{kInvalidPrice};
    Quantity  total_quantity{0};   // sum of all resting order quantities
    uint32_t  order_count{0};

    // Intrusive list head/tail pointers for O(1) FIFO access.
    Order*    head{nullptr};       // oldest order (executed first)
    Order*    tail{nullptr};       // newest order  (executed last)

    // -------------------------------------------------------------------------
    // Linked-list helpers (price-time priority: oldest order first)
    // -------------------------------------------------------------------------
    void push_back(Order* o) noexcept {
        o->next = nullptr;
        o->prev = static_cast<IntrusiveListNode*>(tail);
        if (tail)  tail->next = static_cast<IntrusiveListNode*>(o);
        else       head = o;
        tail = o;
        total_quantity += o->quantity;
        ++order_count;
    }

    void remove(Order* o) noexcept {
        if (o->prev) o->prev->next = o->next;
        else         head = static_cast<Order*>(o->next);

        if (o->next) o->next->prev = o->prev;
        else         tail = static_cast<Order*>(o->prev);

        o->next = nullptr;
        o->prev = nullptr;

        total_quantity -= (o->quantity - o->filled_quantity);
        --order_count;
    }

    [[nodiscard]] bool empty() const noexcept { return head == nullptr; }
    [[nodiscard]] bool valid() const noexcept { return price != kInvalidPrice; }
};

// =============================================================================
//  TradeReport — emitted when two orders match.
// =============================================================================
struct alignas(64) TradeReport {
    OrderId   buy_order_id{kInvalidOrderId};
    OrderId   sell_order_id{kInvalidOrderId};
    Price     price{kInvalidPrice};
    Quantity  quantity{0};
    Nanos     match_nanos{0};      // cycle timestamp of the match event
    Nanos     latency_cycles{0};   // tick_to_trade cycles (rdtsc delta)
};

// =============================================================================
//  OrderMessage — the message delivered through the SPSC queue from gateway.
// =============================================================================
struct alignas(64) OrderMessage {
    OrderAction action{OrderAction::Add};
    Side        side{Side::Buy};
    uint8_t     _pad[6]{};

    OrderId     order_id{kInvalidOrderId};
    OrderId     original_order_id{kInvalidOrderId};  // for MODIFY / CANCEL
    Price       price{kInvalidPrice};
    Quantity    quantity{0};
    Nanos       arrival_cycles{0};  // rdtsc at gateway entry
};

static_assert(std::is_trivially_copyable_v<OrderMessage>,
              "OrderMessage must be trivially copyable for SPSC queue");

}  // namespace ome
