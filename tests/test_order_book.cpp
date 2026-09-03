// =============================================================================
//  test_order_book.cpp  —  GTest Suite for OrderBook
// =============================================================================

#include <gtest/gtest.h>
#include "order_book.hpp"
#include "types.hpp"

#include <vector>

using namespace ome;

// =============================================================================
//  Test fixture with a trade-report collector callback.
// =============================================================================
class OrderBookTest : public ::testing::Test {
protected:
    std::vector<TradeReport> trades_;

    static void collect_trade(const TradeReport& r, void* ctx) {
        auto* self = static_cast<OrderBookTest*>(ctx);
        self->trades_.push_back(r);
    }

    OrderBook book_{&collect_trade, this};

    // Helper: build a standard ADD message.
    static OrderMessage add_msg(OrderId id, Side side,
                                Price price, Quantity qty,
                                OrderId arrival_cycles = 1) {
        OrderMessage m{};
        m.action         = OrderAction::Add;
        m.side           = side;
        m.order_id       = id;
        m.price          = price;
        m.quantity       = qty;
        m.arrival_cycles = arrival_cycles;
        return m;
    }
};

// =============================================================================
//  No match — order rests in book
// =============================================================================
TEST_F(OrderBookTest, NoMatch_OrderRests) {
    book_.add_order(add_msg(1, Side::Buy, 1000, 10));
    EXPECT_EQ(book_.best_bid(), 1000);
    EXPECT_EQ(book_.best_ask(), kInvalidPrice);
    EXPECT_EQ(trades_.size(), 0u);
    EXPECT_EQ(book_.order_count(), 1u);
}

// =============================================================================
//  Full match — buy crosses sell completely
// =============================================================================
TEST_F(OrderBookTest, FullMatch_BuyCrosseSell) {
    // Rest a sell at 1000 qty=5.
    book_.add_order(add_msg(1, Side::Sell, 1000, 5));
    EXPECT_EQ(book_.best_ask(), 1000);

    // Incoming buy at 1000 qty=5 should fully match.
    book_.add_order(add_msg(2, Side::Buy, 1000, 5));
    EXPECT_EQ(book_.best_ask(), kInvalidPrice);  // sell level cleared
    EXPECT_EQ(book_.best_bid(), kInvalidPrice);  // buy fully filled, not resting
    EXPECT_EQ(trades_.size(), 1u);
    EXPECT_EQ(trades_[0].quantity, 5u);
    EXPECT_EQ(trades_[0].price,    1000);
    EXPECT_EQ(trades_[0].buy_order_id,  2u);
    EXPECT_EQ(trades_[0].sell_order_id, 1u);
    EXPECT_EQ(book_.order_count(), 0u);
}

// =============================================================================
//  Partial match — aggressor partially fills, rests remainder
// =============================================================================
TEST_F(OrderBookTest, PartialMatch_AggressorRests) {
    book_.add_order(add_msg(1, Side::Sell, 1000, 3));  // sell 3
    book_.add_order(add_msg(2, Side::Buy,  1000, 7));  // buy  7 — matches 3, rests 4

    ASSERT_EQ(trades_.size(), 1u);
    EXPECT_EQ(trades_[0].quantity, 3u);

    // Buy remainder should rest.
    EXPECT_EQ(book_.best_bid(), 1000);
    EXPECT_EQ(book_.bid_level(1000).total_quantity, 4u);
    EXPECT_EQ(book_.order_count(), 1u);  // only the resting buy remains
}

// =============================================================================
//  Cancel — resting order removed, best price updated
// =============================================================================
TEST_F(OrderBookTest, Cancel_RemovesOrder) {
    book_.add_order(add_msg(1, Side::Buy, 1000, 5));
    book_.add_order(add_msg(2, Side::Buy, 1001, 3));
    EXPECT_EQ(book_.best_bid(), 1001);

    bool ok = book_.cancel_order(2);
    EXPECT_TRUE(ok);
    EXPECT_EQ(book_.best_bid(), 1000);  // falls back to next level
    EXPECT_EQ(book_.order_count(), 1u);
}

// =============================================================================
//  Cancel non-existent order returns false
// =============================================================================
TEST_F(OrderBookTest, Cancel_NonExistent) {
    EXPECT_FALSE(book_.cancel_order(999));
}

// =============================================================================
//  Price-time priority — oldest resting order matched first
// =============================================================================
TEST_F(OrderBookTest, PriceTimePriority) {
    // Two sell orders at the same price — order 1 arrived first.
    book_.add_order(add_msg(1, Side::Sell, 1000, 2, /*ts=*/10));
    book_.add_order(add_msg(2, Side::Sell, 1000, 2, /*ts=*/20));

    // Buy 2 — should fully match against order 1 (oldest).
    book_.add_order(add_msg(3, Side::Buy, 1000, 2, /*ts=*/30));

    ASSERT_EQ(trades_.size(), 1u);
    EXPECT_EQ(trades_[0].sell_order_id, 1u);  // order 1 matched first
    EXPECT_EQ(book_.best_ask(), 1000);         // order 2 still resting
    EXPECT_EQ(book_.ask_level(1000).total_quantity, 2u);
}

// =============================================================================
//  Multiple levels swept — aggressor consumes multiple price levels
// =============================================================================
TEST_F(OrderBookTest, SweepMultipleLevels) {
    book_.add_order(add_msg(1, Side::Sell, 1000, 2));
    book_.add_order(add_msg(2, Side::Sell, 1001, 2));
    book_.add_order(add_msg(3, Side::Sell, 1002, 2));

    // Buy 6 at 1005 — should sweep all three levels.
    book_.add_order(add_msg(4, Side::Buy, 1005, 6));

    EXPECT_EQ(trades_.size(), 3u);
    EXPECT_EQ(book_.best_ask(), kInvalidPrice);
    EXPECT_EQ(book_.best_bid(), kInvalidPrice);
    EXPECT_EQ(book_.order_count(), 0u);
}

// =============================================================================
//  Modify — quantity decrease preserves time priority
// =============================================================================
TEST_F(OrderBookTest, Modify_DecreaseQty) {
    book_.add_order(add_msg(1, Side::Buy, 1000, 10));
    Order* o = book_.modify_order(1, 5);
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(book_.bid_level(1000).total_quantity, 5u);
    EXPECT_EQ(trades_.size(), 0u);
}

// =============================================================================
//  Best bid/ask — tracks correct best across multiple operations
// =============================================================================
TEST_F(OrderBookTest, BestBidAskTracking) {
    EXPECT_EQ(book_.best_bid(), kInvalidPrice);
    EXPECT_EQ(book_.best_ask(), kInvalidPrice);

    book_.add_order(add_msg(1, Side::Buy, 990, 1));
    book_.add_order(add_msg(2, Side::Buy, 995, 1));
    EXPECT_EQ(book_.best_bid(), 995);

    book_.add_order(add_msg(3, Side::Sell, 1005, 1));
    book_.add_order(add_msg(4, Side::Sell, 1000, 1));
    EXPECT_EQ(book_.best_ask(), 1000);

    book_.cancel_order(4);
    EXPECT_EQ(book_.best_ask(), 1005);
}

// =============================================================================
//  Match statistics
// =============================================================================
TEST_F(OrderBookTest, MatchStatistics) {
    book_.add_order(add_msg(1, Side::Sell, 1000, 10));
    book_.add_order(add_msg(2, Side::Buy,  1000, 10));

    EXPECT_EQ(book_.match_count(),  1u);
    EXPECT_EQ(book_.trade_volume(), 10u);
}
