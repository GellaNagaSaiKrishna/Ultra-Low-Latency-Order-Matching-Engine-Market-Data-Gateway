#pragma once
// =============================================================================
//  matching_engine.hpp  —  Single-Threaded Matching Engine Core
//  Phase 2: Matching Engine & Order Book Logic
//
//  Design:
//    - Owns the OrderBook and drives the event loop.
//    - Reads OrderMessage objects from an inbound SPSCQueue (written by the
//      MarketDataGateway thread).
//    - Pushes TradeReport objects to an outbound SPSCQueue for downstream
//      consumers (risk, post-trade, logging).
//    - Runs on a single dedicated thread; no locks required.
// =============================================================================

#include "order_book.hpp"
#include "spsc_queue.hpp"
#include "types.hpp"

#include <atomic>
#include <cstdint>

namespace ome {

// Queue capacities (must be powers of 2).
static constexpr std::size_t kInboundQueueSize  = 1 << 16;  // 65536 slots
static constexpr std::size_t kOutboundQueueSize = 1 << 16;

using InboundQueue  = SPSCQueue<OrderMessage, kInboundQueueSize>;
using OutboundQueue = SPSCQueue<TradeReport,  kOutboundQueueSize>;

class MatchingEngine {
public:
    explicit MatchingEngine(InboundQueue&  inbound,
                            OutboundQueue& outbound) noexcept
        : inbound_(inbound), outbound_(outbound) {

        // Wire the order book to push trade reports into our outbound queue.
        book_.set_trade_callback(&MatchingEngine::on_trade, this);
    }

    MatchingEngine(const MatchingEngine&)            = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    // =========================================================================
    //  run_once() — process all pending messages in the inbound queue.
    //  Call this in a tight loop on the matching thread.
    //  Returns the number of messages processed.
    // =========================================================================
    [[nodiscard]] int run_once() noexcept {
        int processed = 0;
        while (auto msg = inbound_.try_pop()) {
            dispatch(*msg);
            ++processed;
        }
        return processed;
    }

    // =========================================================================
    //  run() — blocking event loop. Spin until stop() is called.
    // =========================================================================
    void run() noexcept {
        running_.store(true, std::memory_order_release);
        while ([[likely]] running_.load(std::memory_order_acquire)) {
            run_once();
        }
    }

    void stop() noexcept {
        running_.store(false, std::memory_order_release);
    }

    // =========================================================================
    //  Diagnostics
    // =========================================================================
    [[nodiscard]] const OrderBook& book()     const noexcept { return book_; }
    [[nodiscard]] uint64_t msg_processed()    const noexcept { return msgs_processed_; }

private:
    // =========================================================================
    //  dispatch — route message to the correct order book operation.
    // =========================================================================
    void dispatch(const OrderMessage& msg) noexcept {
        ++msgs_processed_;
        switch (msg.action) {
            [[likely]]   case OrderAction::Add:
                book_.add_order(msg);
                break;
            case OrderAction::Cancel:
                book_.cancel_order(msg.original_order_id);
                break;
            case OrderAction::Modify:
                book_.modify_order(msg.original_order_id, msg.quantity);
                break;
            case OrderAction::Execute:
                // Direct execution requests (e.g. market orders treated as
                // aggressive limit orders at extreme prices).
                book_.add_order(msg);
                break;
            default:
                break;
        }
    }

    // =========================================================================
    //  on_trade — static callback invoked by OrderBook on each fill.
    // =========================================================================
    static void on_trade(const TradeReport& report, void* ctx) noexcept {
        auto* self = static_cast<MatchingEngine*>(ctx);
        // Best-effort push; drop if outbound queue full to avoid blocking.
        self->outbound_.try_push(report);
        ++self->trades_emitted_;
    }

    // =========================================================================
    //  State
    // =========================================================================
    InboundQueue&  inbound_;
    OutboundQueue& outbound_;
    OrderBook      book_{};

    std::atomic<bool> running_{false};
    uint64_t          msgs_processed_{0};
    uint64_t          trades_emitted_{0};
};

}  // namespace ome
