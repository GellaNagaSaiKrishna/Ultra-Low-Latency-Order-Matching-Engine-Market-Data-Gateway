#pragma once
// =============================================================================
//  order_book.hpp  —  Limit Order Book with Price-Time Priority Matching
//  Phase 2: Matching Engine & Order Book Logic
//
//  Design:
//    - Flat tick-array price levels: buy_levels_[price] / sell_levels_[price].
//      Indexed directly by integer price tick — O(1) level lookup.
//    - Best bid tracked as the highest active buy price tick.
//    - Best ask tracked as the lowest  active sell price tick.
//    - Orders allocated from a MemoryPool — zero hot-path heap allocations.
//    - Matching runs inward from best-bid/best-ask until the incoming order
//      quantity is exhausted or no crossable levels remain.
//    - Emits TradeReport objects for every partial or full fill.
// =============================================================================

#include <array>
#include <cassert>
#include <cstddef>
#include <functional>

#include "types.hpp"
#include "memory_pool.hpp"

namespace ome {

// =============================================================================
//  OrderBook configuration constants
// =============================================================================

/// Number of discrete price ticks supported.
/// Prices must be in [0, kMaxPriceTicks).
/// At 64 bytes per LimitLevel this is ~4 MB per side — fits in L3 cache.
static constexpr std::size_t kMaxPriceTicks = 65536;

/// Maximum number of live resting orders.
static constexpr std::size_t kMaxOrders = 1 << 20;  // 1,048,576

// =============================================================================
//  TradeReportCallback — called synchronously for each match event.
//  Designed to be a lightweight function_ref / raw function pointer.
// =============================================================================
using TradeReportCallback = void (*)(const TradeReport&, void* ctx);

// =============================================================================
//  OrderBook
// =============================================================================
class OrderBook {
public:
    explicit OrderBook(TradeReportCallback cb = nullptr,
                       void* cb_ctx           = nullptr) noexcept
        : trade_cb_(cb), trade_cb_ctx_(cb_ctx) {
        // Initialise all price levels.
        for (Price p = 0; p < static_cast<Price>(kMaxPriceTicks); ++p) {
            buy_levels_[p].price  = p;
            sell_levels_[p].price = p;
        }
    }

    // No copy.
    OrderBook(const OrderBook&)            = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    // =========================================================================
    //  Public API
    // =========================================================================

    /// Add a new resting order. If it is immediately marketable, match it first.
    /// Returns the newly created Order*, or nullptr if the pool is exhausted.
    Order* add_order(const OrderMessage& msg) noexcept;

    /// Cancel a resting order by ID. Returns true on success.
    bool cancel_order(OrderId id) noexcept;

    /// Modify quantity of a resting order (cancel + re-insert preserving time).
    /// Returns the updated Order* or nullptr on failure.
    Order* modify_order(OrderId id, Quantity new_qty) noexcept;

    // =========================================================================
    //  Diagnostics
    // =========================================================================
    [[nodiscard]] Price best_bid() const noexcept { return best_bid_; }
    [[nodiscard]] Price best_ask() const noexcept { return best_ask_; }

    [[nodiscard]] const LimitLevel& bid_level(Price p) const noexcept {
        assert(p >= 0 && p < static_cast<Price>(kMaxPriceTicks));
        return buy_levels_[p];
    }
    [[nodiscard]] const LimitLevel& ask_level(Price p) const noexcept {
        assert(p >= 0 && p < static_cast<Price>(kMaxPriceTicks));
        return sell_levels_[p];
    }

    [[nodiscard]] std::size_t order_count()  const noexcept {
        return kMaxOrders - order_pool_.available();
    }
    [[nodiscard]] uint64_t    match_count()  const noexcept { return match_count_; }
    [[nodiscard]] uint64_t    trade_volume() const noexcept { return trade_volume_; }

    void set_trade_callback(TradeReportCallback cb, void* ctx) noexcept {
        trade_cb_     = cb;
        trade_cb_ctx_ = ctx;
    }

private:
    // =========================================================================
    //  Internal helpers
    // =========================================================================

    /// Run the matching loop for a newly arrived order.
    void match(Order* incoming) noexcept;

    /// Emit a fill event and update book state.
    void fill(Order* resting, Order* aggressor, Quantity qty) noexcept;

    /// Register order in the lookup map and add to its price level.
    void rest_order(Order* o) noexcept;

    /// Remove order from its price level and recycle to pool.
    void retire_order(Order* o) noexcept;

    /// Update best_bid_ walking downward from current best.
    void update_best_bid(Price from_price) noexcept;

    /// Update best_ask_ walking upward from current best.
    void update_best_ask(Price from_price) noexcept;

    // =========================================================================
    //  State
    // =========================================================================

    /// Flat price-level arrays — the core cache-friendly structure.
    alignas(64) std::array<LimitLevel, kMaxPriceTicks> buy_levels_{};
    alignas(64) std::array<LimitLevel, kMaxPriceTicks> sell_levels_{};

    /// Order memory pool — no heap after construction.
    MemoryPool<Order, kMaxOrders> order_pool_{};

    /// Order-ID → Order* lookup table for O(1) cancel/modify.
    /// Sized to kMaxOrders; index = order_id % kMaxOrders.
    std::array<Order*, kMaxOrders> order_map_{};

    Price best_bid_{kInvalidPrice};
    Price best_ask_{kInvalidPrice};

    TradeReportCallback trade_cb_{nullptr};
    void*               trade_cb_ctx_{nullptr};

    uint64_t match_count_{0};
    uint64_t trade_volume_{0};
};

}  // namespace ome
