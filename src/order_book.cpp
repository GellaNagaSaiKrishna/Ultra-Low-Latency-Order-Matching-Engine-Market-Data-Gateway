// =============================================================================
//  order_book.cpp  —  Limit Order Book Implementation
//  Phase 2: Matching Engine & Order Book Logic
// =============================================================================

#include "order_book.hpp"
#include "rdtsc.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace ome {

// =============================================================================
//  add_order
// =============================================================================
Order* OrderBook::add_order(const OrderMessage& msg) noexcept {
    assert(msg.price >= 0 &&
           msg.price < static_cast<Price>(kMaxPriceTicks) &&
           "Price out of tick-array range");
    assert(msg.quantity > 0 && "Order quantity must be > 0");

    // Allocate from pool (zero heap allocation after startup).
    Order* o = order_pool_.construct();
    if ([[unlikely]] o == nullptr) return nullptr;  // pool exhausted

    o->id              = msg.order_id;
    o->side            = msg.side;
    o->price           = msg.price;
    o->quantity        = msg.quantity;
    o->filled_quantity = 0;
    o->status          = OrderStatus::Active;
    o->timestamp       = msg.arrival_cycles;
    o->entry_nanos     = rdtsc();
    o->next            = nullptr;
    o->prev            = nullptr;

    // Register in order-id map.
    const std::size_t slot = static_cast<std::size_t>(msg.order_id) % kMaxOrders;
    order_map_[slot] = o;

    // Attempt immediate matching first.
    match(o);

    // If not fully filled, rest it in the book.
    if (o->status != OrderStatus::Filled) {
        rest_order(o);
    }

    return o;
}

// =============================================================================
//  cancel_order
// =============================================================================
bool OrderBook::cancel_order(OrderId id) noexcept {
    const std::size_t slot = static_cast<std::size_t>(id) % kMaxOrders;
    Order* o = order_map_[slot];

    if ([[unlikely]] o == nullptr || o->id != id) return false;
    if ([[unlikely]] o->status != OrderStatus::Active &&
                     o->status != OrderStatus::PartiallyFilled) return false;

    o->status = OrderStatus::Cancelled;
    order_map_[slot] = nullptr;

    // Remove from price level.
    LimitLevel& level = (o->side == Side::Buy)
                            ? buy_levels_[o->price]
                            : sell_levels_[o->price];
    level.remove(o);

    if (level.empty()) {
        if (o->side == Side::Buy  && o->price == best_bid_)
            update_best_bid(best_bid_);
        if (o->side == Side::Sell && o->price == best_ask_)
            update_best_ask(best_ask_);
    }

    order_pool_.destroy(o);
    return true;
}

// =============================================================================
//  modify_order
// =============================================================================
Order* OrderBook::modify_order(OrderId id, Quantity new_qty) noexcept {
    const std::size_t slot = static_cast<std::size_t>(id) % kMaxOrders;
    Order* o = order_map_[slot];

    if ([[unlikely]] o == nullptr || o->id != id) return nullptr;
    if ([[unlikely]] o->status != OrderStatus::Active &&
                     o->status != OrderStatus::PartiallyFilled) return nullptr;

    const Quantity remaining = o->quantity - o->filled_quantity;

    if (new_qty <= remaining) {
        // Quantity decrease: update in-place (preserves time priority).
        LimitLevel& level = (o->side == Side::Buy)
                                ? buy_levels_[o->price]
                                : sell_levels_[o->price];
        level.total_quantity -= (remaining - new_qty);
        o->quantity = o->filled_quantity + new_qty;
    } else {
        // Quantity increase: re-insert at back (loses time priority).
        cancel_order(id);

        OrderMessage msg{};
        msg.action         = OrderAction::Add;
        msg.side           = o->side;
        msg.order_id       = id;
        msg.price          = o->price;
        msg.quantity       = new_qty;
        msg.arrival_cycles = rdtsc();
        return add_order(msg);
    }

    return o;
}

// =============================================================================
//  match  —  price-time priority matching loop
// =============================================================================
void OrderBook::match(Order* incoming) noexcept {
    if (incoming->side == Side::Buy) {
        // Buy order: consume sell levels from best_ask_ downward to incoming price.
        while (best_ask_ != kInvalidPrice &&
               best_ask_ <= incoming->price &&
               incoming->quantity > incoming->filled_quantity) {

            LimitLevel& level = sell_levels_[best_ask_];
            if (level.empty()) {
                update_best_ask(best_ask_);
                continue;
            }

            // Walk orders in this level front-to-back (time priority).
            Order* resting = level.head;
            while (resting != nullptr &&
                   incoming->quantity > incoming->filled_quantity) {

                const Quantity resting_remain =
                    resting->quantity - resting->filled_quantity;
                const Quantity aggressor_remain =
                    incoming->quantity - incoming->filled_quantity;
                const Quantity fill_qty =
                    std::min(resting_remain, aggressor_remain);

                Order* next_resting = static_cast<Order*>(resting->next);
                fill(resting, incoming, fill_qty);
                resting = next_resting;
            }

            if (level.empty()) {
                update_best_ask(best_ask_);
            }
        }
    } else {
        // Sell order: consume buy levels from best_bid_ downward.
        while (best_bid_ != kInvalidPrice &&
               best_bid_ >= incoming->price &&
               incoming->quantity > incoming->filled_quantity) {

            LimitLevel& level = buy_levels_[best_bid_];
            if (level.empty()) {
                update_best_bid(best_bid_);
                continue;
            }

            Order* resting = level.head;
            while (resting != nullptr &&
                   incoming->quantity > incoming->filled_quantity) {

                const Quantity resting_remain =
                    resting->quantity - resting->filled_quantity;
                const Quantity aggressor_remain =
                    incoming->quantity - incoming->filled_quantity;
                const Quantity fill_qty =
                    std::min(resting_remain, aggressor_remain);

                Order* next_resting = static_cast<Order*>(resting->next);
                fill(resting, incoming, fill_qty);
                resting = next_resting;
            }

            if (level.empty()) {
                update_best_bid(best_bid_);
            }
        }
    }
}

// =============================================================================
//  fill  —  record a match, update quantities, emit TradeReport
// =============================================================================
void OrderBook::fill(Order* resting, Order* aggressor, Quantity qty) noexcept {
    resting->filled_quantity  += qty;
    aggressor->filled_quantity += qty;

    LimitLevel& level = (resting->side == Side::Buy)
                            ? buy_levels_[resting->price]
                            : sell_levels_[resting->price];
    level.total_quantity -= qty;

    if (resting->filled_quantity >= resting->quantity) {
        resting->status = OrderStatus::Filled;
        level.remove(resting);

        const std::size_t slot =
            static_cast<std::size_t>(resting->id) % kMaxOrders;
        order_map_[slot] = nullptr;
        order_pool_.destroy(resting);
    } else {
        resting->status = OrderStatus::PartiallyFilled;
    }

    if (aggressor->filled_quantity >= aggressor->quantity) {
        aggressor->status = OrderStatus::Filled;
    } else {
        aggressor->status = OrderStatus::PartiallyFilled;
    }

    ++match_count_;
    trade_volume_ += qty;

    if ([[likely]] trade_cb_ != nullptr) {
        TradeReport report{};
        if (resting->side == Side::Buy) {
            report.buy_order_id  = resting->id;
            report.sell_order_id = aggressor->id;
        } else {
            report.buy_order_id  = aggressor->id;
            report.sell_order_id = resting->id;
        }
        report.price         = resting->price;
        report.quantity      = qty;
        report.match_nanos   = rdtsc();
        report.latency_cycles =
            (aggressor->entry_nanos > 0)
                ? (report.match_nanos - aggressor->entry_nanos)
                : 0;
        trade_cb_(report, trade_cb_ctx_);
    }
}

// =============================================================================
//  rest_order — add to price level and update best bid/ask
// =============================================================================
void OrderBook::rest_order(Order* o) noexcept {
    LimitLevel& level = (o->side == Side::Buy)
                            ? buy_levels_[o->price]
                            : sell_levels_[o->price];
    level.push_back(o);

    if (o->side == Side::Buy) {
        if (best_bid_ == kInvalidPrice || o->price > best_bid_)
            best_bid_ = o->price;
    } else {
        if (best_ask_ == kInvalidPrice || o->price < best_ask_)
            best_ask_ = o->price;
    }
}

// =============================================================================
//  retire_order — remove from price level, release pool slot
// =============================================================================
void OrderBook::retire_order(Order* o) noexcept {
    LimitLevel& level = (o->side == Side::Buy)
                            ? buy_levels_[o->price]
                            : sell_levels_[o->price];
    level.remove(o);

    const std::size_t slot =
        static_cast<std::size_t>(o->id) % kMaxOrders;
    order_map_[slot] = nullptr;
    order_pool_.destroy(o);
}

// =============================================================================
//  update_best_bid / update_best_ask
// =============================================================================
void OrderBook::update_best_bid(Price from_price) noexcept {
    for (Price p = from_price; p >= 0; --p) {
        if (!buy_levels_[p].empty()) {
            best_bid_ = p;
            return;
        }
    }
    best_bid_ = kInvalidPrice;
}

void OrderBook::update_best_ask(Price from_price) noexcept {
    for (Price p = from_price;
         p < static_cast<Price>(kMaxPriceTicks); ++p) {
        if (!sell_levels_[p].empty()) {
            best_ask_ = p;
            return;
        }
    }
    best_ask_ = kInvalidPrice;
}

}  // namespace ome
