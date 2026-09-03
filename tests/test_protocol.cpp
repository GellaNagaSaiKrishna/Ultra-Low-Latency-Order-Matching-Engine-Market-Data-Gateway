// =============================================================================
//  test_protocol.cpp  —  GTest Suite for Binary Wire Protocol
// =============================================================================

#include <gtest/gtest.h>
#include "protocol.hpp"
#include "types.hpp"

#include <array>
#include <cstring>

using namespace ome;
using namespace ome::proto;

// =============================================================================
//  Round-trip: encode → decode
// =============================================================================
TEST(Protocol, RoundTrip) {
    OrderMessage original{};
    original.action            = OrderAction::Add;
    original.side              = Side::Buy;
    original.order_id          = 42;
    original.original_order_id = 0;
    original.price             = 1000;
    original.quantity          = 5;

    std::array<uint8_t, kFrameSize> buf{};
    encode(original, 12345678900ULL, buf.data());

    OrderMessage decoded{};
    ASSERT_TRUE(decode(buf.data(), buf.size(), decoded));

    EXPECT_EQ(decoded.action,            original.action);
    EXPECT_EQ(decoded.side,              original.side);
    EXPECT_EQ(decoded.order_id,          original.order_id);
    EXPECT_EQ(decoded.original_order_id, original.original_order_id);
    EXPECT_EQ(decoded.price,             original.price);
    EXPECT_EQ(decoded.quantity,          original.quantity);
}

// =============================================================================
//  Bad magic — decode returns false
// =============================================================================
TEST(Protocol, BadMagic) {
    OrderMessage msg{};
    std::array<uint8_t, kFrameSize> buf{};
    encode(msg, 0, buf.data());

    // Corrupt first byte.
    buf[0] = 0xFF;

    OrderMessage out{};
    EXPECT_FALSE(decode(buf.data(), buf.size(), out));
}

// =============================================================================
//  Short buffer — decode returns false
// =============================================================================
TEST(Protocol, ShortBuffer) {
    std::array<uint8_t, kFrameSize - 1> buf{};
    OrderMessage out{};
    EXPECT_FALSE(decode(buf.data(), buf.size(), out));
}

// =============================================================================
//  All order actions round-trip correctly
// =============================================================================
TEST(Protocol, AllActions) {
    const std::array<OrderAction, 4> actions = {
        OrderAction::Add, OrderAction::Cancel,
        OrderAction::Modify, OrderAction::Execute
    };

    for (auto action : actions) {
        OrderMessage msg{};
        msg.action   = action;
        msg.order_id = 1;
        msg.price    = 500;
        msg.quantity = 1;

        std::array<uint8_t, kFrameSize> buf{};
        encode(msg, 0, buf.data());

        OrderMessage out{};
        ASSERT_TRUE(decode(buf.data(), buf.size(), out));
        EXPECT_EQ(out.action, action);
    }
}

// =============================================================================
//  Frame size constant is correct
// =============================================================================
TEST(Protocol, FrameSizeConstant) {
    EXPECT_EQ(kFrameSize, sizeof(WireHeader) + sizeof(WireOrder));
    EXPECT_EQ(kFrameSize, 48u);
}
