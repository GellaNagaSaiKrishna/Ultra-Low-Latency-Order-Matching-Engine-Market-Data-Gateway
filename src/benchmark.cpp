// =============================================================================
//  benchmark.cpp  —  End-to-End Latency & Throughput Benchmark
//  Phase 4: Benchmarking & Low-Level Optimization
//
//  Drives the full Gateway → SPSC Queue → MatchingEngine pipeline with a
//  synthetic order feed, collects per-order tick-to-trade cycle deltas, and
//  reports latency percentiles (p50, p90, p99, p99.9) in nanoseconds.
//
//  Build:
//    cmake --build build --target benchmark
//  Run:
//    ./build/benchmark [num_orders]
// =============================================================================

#include "market_data_gateway.hpp"
#include "matching_engine.hpp"
#include "rdtsc.hpp"
#include "types.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace ome;

// =============================================================================
//  Latency statistics helper
// =============================================================================
struct LatencyStats {
    double p50_ns{0};
    double p90_ns{0};
    double p99_ns{0};
    double p999_ns{0};
    double mean_ns{0};
    double min_ns{0};
    double max_ns{0};
    std::size_t count{0};
};

static LatencyStats compute_stats(std::vector<double>& samples) {
    if (samples.empty()) return {};
    std::sort(samples.begin(), samples.end());
    LatencyStats s{};
    s.count  = samples.size();
    s.min_ns = samples.front();
    s.max_ns = samples.back();

    double sum = 0.0;
    for (double v : samples) sum += v;
    s.mean_ns = sum / static_cast<double>(s.count);

    auto pct = [&](double p) -> double {
        const std::size_t idx = static_cast<std::size_t>(
            p * static_cast<double>(s.count - 1));
        return samples[idx];
    };

    s.p50_ns  = pct(0.50);
    s.p90_ns  = pct(0.90);
    s.p99_ns  = pct(0.99);
    s.p999_ns = pct(0.999);
    return s;
}

// =============================================================================
//  Synthetic order feed generator
// =============================================================================
static OrderMessage make_add_order(OrderId id, Side side,
                                   Price price, Quantity qty) noexcept {
    OrderMessage msg{};
    msg.action   = OrderAction::Add;
    msg.side     = side;
    msg.order_id = id;
    msg.price    = price;
    msg.quantity = qty;
    return msg;
}

// =============================================================================
//  main
// =============================================================================
int main(int argc, char* argv[]) {
    const std::size_t num_orders = (argc > 1)
        ? static_cast<std::size_t>(std::atoll(argv[1]))
        : 1'000'000ULL;

    std::printf("=================================================================\n");
    std::printf("  Ultra-Low Latency Order Matching Engine  —  Benchmark\n");
    std::printf("=================================================================\n");
    std::printf("  Orders to process : %zu\n\n", num_orders);

    // -------------------------------------------------------------------------
    // Calibrate TSC frequency.
    // -------------------------------------------------------------------------
    std::printf("  Calibrating TSC frequency (10ms spin)...\n");
    const double cpns = cycles_per_ns();
    std::printf("  TSC frequency      : %.2f GHz\n\n", cpns);

    // -------------------------------------------------------------------------
    // Set up the pipeline.
    // -------------------------------------------------------------------------
    InboundQueue  inbound{};
    OutboundQueue outbound{};

    // Collect latency samples from the outbound trade reports.
    std::vector<double> latency_samples;
    latency_samples.reserve(num_orders);

    // We need a custom callback to capture latencies from trade reports.
    // Use the outbound queue consumer loop below.

    MatchingEngine engine(inbound, outbound);
    MarketDataGateway gateway(inbound);

    // -------------------------------------------------------------------------
    // Phase 1: warmup — fill the instruction cache.
    // -------------------------------------------------------------------------
    std::printf("  Warming up (100k orders)...\n");
    constexpr std::size_t kWarmup = 100'000;
    for (std::size_t i = 1; i <= kWarmup; ++i) {
        const Side  side  = (i % 2 == 0) ? Side::Buy : Side::Sell;
        const Price price = static_cast<Price>(1000 + (i % 100));
        const auto  msg   = make_add_order(i, side, price, 10);
        gateway.inject_order(msg);
        engine.run_once();
    }
    // Drain outbound.
    while (outbound.try_pop()) {}

    // -------------------------------------------------------------------------
    // Phase 2: timed run — alternating buy/sell at crossing prices.
    //   Buy  @ 1000–1049 (bid side)
    //   Sell @ 1000–1049 (crosses bids immediately → trades)
    // -------------------------------------------------------------------------
    std::printf("  Running timed benchmark (%zu orders)...\n\n", num_orders);

    const auto wall_start = std::chrono::steady_clock::now();
    const uint64_t tsc_start = rdtsc_start();

    OrderId next_id = kWarmup + 1;
    std::size_t submitted = 0;

    while (submitted < num_orders) {
        // Submit a batch of adds.
        constexpr std::size_t kBatch = 64;
        for (std::size_t b = 0; b < kBatch && submitted < num_orders; ++b) {
            const bool is_buy = (next_id % 2 == 0);
            const Side side   = is_buy ? Side::Buy : Side::Sell;
            const Price price = static_cast<Price>(1000 + (next_id % 50));
            auto msg = make_add_order(next_id++, side, price, 1);
            msg.arrival_cycles = rdtsc();
            gateway.inject_order(msg);
            ++submitted;
        }

        // Drive matching engine.
        engine.run_once();

        // Drain trade reports and collect latencies.
        while (auto report = outbound.try_pop()) {
            if (report->latency_cycles > 0) {
                latency_samples.push_back(
                    cycles_to_ns(report->latency_cycles, cpns));
            }
        }
    }

    // Final drain.
    engine.run_once();
    while (auto report = outbound.try_pop()) {
        if (report->latency_cycles > 0) {
            latency_samples.push_back(
                cycles_to_ns(report->latency_cycles, cpns));
        }
    }

    const uint64_t tsc_end = rdtsc_end();
    const auto wall_end = std::chrono::steady_clock::now();

    // -------------------------------------------------------------------------
    // Results.
    // -------------------------------------------------------------------------
    const double wall_ms = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            wall_end - wall_start).count()) / 1000.0;
    const double total_cycles =
        static_cast<double>(tsc_end - tsc_start);
    const double ops_per_sec =
        static_cast<double>(num_orders) / (wall_ms / 1e3);

    std::printf("=================================================================\n");
    std::printf("  Throughput Results\n");
    std::printf("-----------------------------------------------------------------\n");
    std::printf("  Total orders      : %zu\n", num_orders);
    std::printf("  Wall time         : %.3f ms\n", wall_ms);
    std::printf("  Throughput        : %.2f M ops/sec\n", ops_per_sec / 1e6);
    std::printf("  Total TSC cycles  : %.0f\n", total_cycles);
    std::printf("  Cycles / order    : %.1f\n",
                total_cycles / static_cast<double>(num_orders));

    std::printf("\n");
    std::printf("  Tick-to-Trade Latency (%zu trade reports)\n",
                latency_samples.size());
    std::printf("-----------------------------------------------------------------\n");

    if (latency_samples.empty()) {
        std::printf("  No trade reports collected (no matches occurred).\n");
    } else {
        const auto s = compute_stats(latency_samples);
        std::printf("  Min               : %8.1f ns\n", s.min_ns);
        std::printf("  Mean              : %8.1f ns\n", s.mean_ns);
        std::printf("  p50               : %8.1f ns\n", s.p50_ns);
        std::printf("  p90               : %8.1f ns\n", s.p90_ns);
        std::printf("  p99               : %8.1f ns\n", s.p99_ns);
        std::printf("  p99.9             : %8.1f ns\n", s.p999_ns);
        std::printf("  Max               : %8.1f ns\n", s.max_ns);

        // Check against targets from plan.md.
        std::printf("\n  Target validation:\n");
        const char* mean_ok = (s.mean_ns  < 300.0) ? "PASS" : "FAIL";
        const char* p999_ok = (s.p999_ns  < 1000.0) ? "PASS" : "FAIL";
        const char* tput_ok = (ops_per_sec > 5e6)    ? "PASS" : "FAIL";
        std::printf("    Mean < 300 ns   : %s  (%.1f ns)\n", mean_ok,  s.mean_ns);
        std::printf("    p99.9 < 1 µs    : %s  (%.1f ns)\n", p999_ok, s.p999_ns);
        std::printf("    > 5M ops/sec    : %s  (%.2f M/s)\n", tput_ok,
                    ops_per_sec / 1e6);
    }
    std::printf("=================================================================\n");

    return 0;
}
