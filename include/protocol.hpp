#pragma once
// =============================================================================
//  protocol.hpp  —  Binary Wire Protocol for Order Feed
//  Phase 3: Zero-Copy Network & IPC Interface
//
//  Framing:
//    [ WireHeader (8 bytes) ][ WireOrder (40 bytes) ]
//    Total frame size: 48 bytes
//
//  Design:
//    - Packed structs eliminate padding for exact wire layout.
//    - All multi-byte integers in little-endian (native x86 byte order).
//    - Magic + version enable fast frame validation without full parse.
//    - Zero-copy: decoder reinterpret_casts directly into these structs.
// =============================================================================

#include <cstdint>
#include <cstring>
#include "types.hpp"

namespace ome {
namespace proto {

// Magic bytes: 'O' 'M' 'E' version(1)
static constexpr uint32_t kMagic   = 0x01454D4F;  // "OME\x01"
static constexpr uint8_t  kVersion = 1;

// Total wire frame size.
static constexpr std::size_t kHeaderSize = 8;
static constexpr std::size_t kBodySize   = 40;
static constexpr std::size_t kFrameSize  = kHeaderSize + kBodySize;

// =============================================================================
//  WireHeader — 8 bytes, begins every frame
// =============================================================================
#pragma pack(push, 1)
struct WireHeader {
    uint32_t magic;       // must equal kMagic
    uint8_t  version;     // must equal kVersion
    uint8_t  action;      // OrderAction enum value
    uint8_t  side;        // Side enum value
    uint8_t  _reserved;   // padding for alignment of body
};
static_assert(sizeof(WireHeader) == 8, "WireHeader must be exactly 8 bytes");

// =============================================================================
//  WireOrder — 40 bytes, follows WireHeader
// =============================================================================
struct WireOrder {
    uint64_t order_id;          // unique order identifier
    uint64_t original_order_id; // for CANCEL / MODIFY; 0 for ADD
    int64_t  price;             // integer tick price
    uint64_t quantity;          // order quantity
    uint64_t timestamp_ns;      // sender wall-clock nanoseconds
};
static_assert(sizeof(WireOrder) == 40, "WireOrder must be exactly 40 bytes");
#pragma pack(pop)

static_assert(sizeof(WireHeader) + sizeof(WireOrder) == kFrameSize,
              "Frame size mismatch");

// =============================================================================
//  Decode — zero-copy wire → domain message conversion.
//  buf must point to at least kFrameSize bytes.
//  Returns false if the frame is invalid (bad magic / version).
// =============================================================================
[[nodiscard]] inline bool decode(const uint8_t* buf,
                                  std::size_t    len,
                                  OrderMessage&  out) noexcept {
    if ([[unlikely]] len < kFrameSize) return false;

    const auto* hdr  = reinterpret_cast<const WireHeader*>(buf);
    const auto* body = reinterpret_cast<const WireOrder*>(buf + kHeaderSize);

    // Validate magic and version.
    if ([[unlikely]] hdr->magic   != kMagic)   return false;
    if ([[unlikely]] hdr->version != kVersion)  return false;

    out.action            = static_cast<OrderAction>(hdr->action);
    out.side              = static_cast<Side>(hdr->side);
    out.order_id          = body->order_id;
    out.original_order_id = body->original_order_id;
    out.price             = body->price;
    out.quantity          = body->quantity;
    // arrival_cycles is stamped by the gateway, not the wire.
    out.arrival_cycles    = 0;

    return true;
}

// =============================================================================
//  Encode — domain message → wire frame (for testing / simulation).
//  buf must be at least kFrameSize bytes.
// =============================================================================
inline void encode(const OrderMessage& msg,
                   uint64_t            timestamp_ns,
                   uint8_t*            buf) noexcept {
    auto* hdr  = reinterpret_cast<WireHeader*>(buf);
    auto* body = reinterpret_cast<WireOrder*>(buf + kHeaderSize);

    hdr->magic     = kMagic;
    hdr->version   = kVersion;
    hdr->action    = static_cast<uint8_t>(msg.action);
    hdr->side      = static_cast<uint8_t>(msg.side);
    hdr->_reserved = 0;

    body->order_id          = msg.order_id;
    body->original_order_id = msg.original_order_id;
    body->price             = msg.price;
    body->quantity          = msg.quantity;
    body->timestamp_ns      = timestamp_ns;
}

}  // namespace proto
}  // namespace ome
