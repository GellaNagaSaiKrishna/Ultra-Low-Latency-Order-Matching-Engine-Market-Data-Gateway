#pragma once
// =============================================================================
//  rdtsc.hpp  —  Cycle-Accurate Hardware Timestamp
//  Phase 4: Benchmarking Infrastructure (included early for use in types)
//
//  rdtsc()  — serialising read (CPUID + RDTSC) for start measurements.
//  rdtscp() — read with in-order guarantee (RDTSCP) for end measurements.
//
//  On Linux x86-64 this is the canonical approach used by the Linux kernel
//  and high-frequency trading systems for cycle-accurate profiling.
//  On non-x86 platforms a fallback using std::chrono is provided.
// =============================================================================

#include <cstdint>

namespace ome {

#if defined(__x86_64__) || defined(_M_X64)

/// Serialising RDTSC: forces all prior instructions to complete.
/// Use this BEFORE the code region being timed.
[[nodiscard]] inline uint64_t rdtsc_start() noexcept {
    uint32_t hi, lo;
    __asm__ volatile(
        "cpuid\n\t"   // serialise
        "rdtsc\n\t"
        : "=a"(lo), "=d"(hi)
        : "a"(0)
        : "rbx", "rcx"
    );
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

/// RDTSCP: in-order read, includes processor ID.
/// Use this AFTER the code region being timed.
[[nodiscard]] inline uint64_t rdtsc_end() noexcept {
    uint32_t hi, lo, aux;
    __asm__ volatile(
        "rdtscp\n\t"
        : "=a"(lo), "=d"(hi), "=c"(aux)
        :
        :
    );
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

/// Plain RDTSC — fast, non-serialising. Suitable for in-loop measurements
/// where the surrounding context is already serialised.
[[nodiscard]] inline uint64_t rdtsc() noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ia32_rdtsc();
#else
    uint32_t hi, lo;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
#endif
}

#else  // Non-x86 fallback using std::chrono

#include <chrono>

[[nodiscard]] inline uint64_t rdtsc_start() noexcept {
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

[[nodiscard]] inline uint64_t rdtsc_end() noexcept {
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

[[nodiscard]] inline uint64_t rdtsc() noexcept {
    return rdtsc_start();
}

#endif

/// Spin-calibrate approximate nanoseconds-per-cycle at runtime.
/// Call once at startup; not suitable for hot-path use.
[[nodiscard]] inline double cycles_per_ns() noexcept {
    // We cannot use rdtsc on non-x86, so just return 1.0 for the fallback.
#if defined(__x86_64__) || defined(_M_X64)
    using Clock = std::chrono::steady_clock;
    const uint64_t t0_cycles = rdtsc_start();
    const auto     t0_wall   = Clock::now();

    // Burn ~10ms of wall time to accumulate enough cycles.
    volatile uint64_t dummy = 0;
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               Clock::now() - t0_wall).count() < 10) {
        ++dummy;
    }

    const uint64_t t1_cycles = rdtsc_end();
    const auto     t1_wall   = Clock::now();

    const double elapsed_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            t1_wall - t0_wall).count());
    const double elapsed_cycles =
        static_cast<double>(t1_cycles - t0_cycles);

    return elapsed_cycles / elapsed_ns;
#else
    return 1.0;
#endif
}

/// Convert a cycle delta to nanoseconds using a pre-calibrated cpns value.
[[nodiscard]] inline double cycles_to_ns(uint64_t cycles,
                                          double cpns) noexcept {
    return static_cast<double>(cycles) / cpns;
}

}  // namespace ome
