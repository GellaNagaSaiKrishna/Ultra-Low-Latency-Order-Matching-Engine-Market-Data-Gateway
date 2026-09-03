#pragma once
// =============================================================================
//  market_data_gateway.hpp  —  Zero-Copy Market Data Gateway
//  Phase 3: Zero-Copy Network & IPC Interface
//
//  Design:
//    - Receives raw byte buffers (simulating UDP datagrams or shared-memory
//      ring slots) and decodes them with zero copy into OrderMessage structs.
//    - Stamps the rdtsc cycle counter at the point of decode (arrival_cycles)
//      for accurate tick-to-trade latency measurement.
//    - Pushes decoded messages into the SPSC inbound queue consumed by
//      MatchingEngine.
//    - Runs on a separate "gateway" thread; writes the SPSC queue (producer).
//    - Frame validation (magic + version) drops malformed frames silently.
// =============================================================================

#include "matching_engine.hpp"
#include "protocol.hpp"
#include "rdtsc.hpp"
#include "spsc_queue.hpp"
#include "types.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ome {

class MarketDataGateway {
public:
    explicit MarketDataGateway(InboundQueue& queue) noexcept
        : queue_(queue) {}

    MarketDataGateway(const MarketDataGateway&)            = delete;
    MarketDataGateway& operator=(const MarketDataGateway&) = delete;

    // =========================================================================
    //  process_frame()
    //  Primary hot-path entry point. Called for each arriving network frame.
    //  buf  — pointer to raw bytes.
    //  len  — byte count of the frame.
    //  Returns true if the frame was valid and queued, false otherwise.
    // =========================================================================
    [[nodiscard]] bool process_frame(const uint8_t* buf,
                                      std::size_t    len) noexcept {
        // Stamp arrival before any processing (rdtsc first thing in).
        const uint64_t arrival = rdtsc();

        OrderMessage msg{};
        if ([[unlikely]] !proto::decode(buf, len, msg)) {
            ++dropped_frames_;
            return false;
        }

        msg.arrival_cycles = arrival;

        // Best-effort enqueue; if full, drop (backpressure handled upstream).
        if ([[unlikely]] !queue_.try_push(msg)) {
            ++dropped_frames_;
            return false;
        }

        ++accepted_frames_;
        return true;
    }

    // =========================================================================
    //  process_frame(span) — convenience overload for std::span<const uint8_t>
    // =========================================================================
    [[nodiscard]] bool process_frame(std::span<const uint8_t> frame) noexcept {
        return process_frame(frame.data(), frame.size());
    }

    // =========================================================================
    //  inject_order() — direct injection without wire encode/decode.
    //  Used in benchmarks and unit tests to bypass network serialisation.
    // =========================================================================
    [[nodiscard]] bool inject_order(OrderMessage msg) noexcept {
        if (msg.arrival_cycles == 0) {
            msg.arrival_cycles = rdtsc();
        }
        if ([[unlikely]] !queue_.try_push(msg)) {
            ++dropped_frames_;
            return false;
        }
        ++accepted_frames_;
        return true;
    }

    // =========================================================================
    //  Diagnostics
    // =========================================================================
    [[nodiscard]] uint64_t accepted_frames() const noexcept { return accepted_frames_; }
    [[nodiscard]] uint64_t dropped_frames()  const noexcept { return dropped_frames_;  }
    void reset_counters() noexcept { accepted_frames_ = 0; dropped_frames_ = 0; }

private:
    InboundQueue& queue_;
    uint64_t      accepted_frames_{0};
    uint64_t      dropped_frames_{0};
};

}  // namespace ome
