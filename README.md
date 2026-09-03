# Ultra-Low Latency Order Matching Engine & Market Data Gateway

A production-grade, single-threaded, zero-allocation **Limit Order Book (LOB) matching engine** and **zero-copy market data gateway** written in **C++20**.

Designed for high-frequency trading (HFT) and electronic exchanges where trade execution latency is measured in **nanoseconds**.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│  Network / Shared Memory                                             │
│  Raw UDP frames  ──►  MarketDataGateway  (rdtsc stamp on arrival)   │
│                              │                                       │
│                   Zero-copy decode (reinterpret_cast)                │
│                              │                                       │
│                     SPSC Inbound Queue  (lock-free)                  │
│                              │                                       │
│                      MatchingEngine                                  │
│                              │                                       │
│                         OrderBook                                    │
│                    ┌─────────┴──────────┐                           │
│              Bid Tick Array       Ask Tick Array                     │
│             (flat, 64KB each)    (flat, 64KB each)                  │
│              price → LimitLevel   price → LimitLevel                │
│                       │                    │                         │
│              Intrusive Order Lists  (FIFO per level)                │
│                              │                                       │
│                   TradeReport Callback                               │
│                              │                                       │
│                    SPSC Outbound Queue                               │
│                              │                                       │
│                  Downstream (risk / logging)                         │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Key Design Principles

| Principle | Implementation |
|---|---|
| **Zero hot-path allocation** | `MemoryPool<T,N>` slab allocator pre-allocates all `Order` objects at startup |
| **Lock-free IPC** | `SPSCQueue<T,N>` ring buffer with `memory_order_acquire/release` |
| **False-share elimination** | Head/tail atomics on separate 64-byte cache lines |
| **O(1) price lookup** | Flat tick-array `LimitLevel[65536]` per side — index = price tick |
| **Zero-copy parsing** | `reinterpret_cast<const WireHeader*>(buf)` — no memcpy, no string allocations |
| **Cache-line aligned structs** | `alignas(64)` on `Order`, `LimitLevel`, `TradeReport`, `OrderBook` fields |
| **Branchless hot paths** | `[[likely]]` / `[[unlikely]]` attributes, bitwise index masking |
| **Cycle-accurate timing** | `rdtsc` / `rdtscp` with CPUID serialisation for p99.9 latency measurement |

---

## Project Structure

```
.
├── include/
│   ├── memory_pool.hpp          # Phase 1: Slab allocator
│   ├── spsc_queue.hpp           # Phase 1: Lock-free ring buffer
│   ├── intrusive_list.hpp       # Phase 1: Intrusive doubly-linked list
│   ├── types.hpp                # Phase 2: POD domain types
│   ├── order_book.hpp           # Phase 2: LOB declaration
│   ├── matching_engine.hpp      # Phase 2: Engine event loop
│   ├── protocol.hpp             # Phase 3: Binary wire protocol
│   ├── market_data_gateway.hpp  # Phase 3: Zero-copy frame receiver
│   └── rdtsc.hpp                # Phase 4: Cycle-accurate timestamp
├── src/
│   ├── order_book.cpp           # Phase 2: LOB implementation
│   └── benchmark.cpp            # Phase 4: End-to-end latency benchmark
├── tests/
│   ├── test_memory_pool.cpp
│   ├── test_spsc_queue.cpp
│   ├── test_order_book.cpp
│   └── test_protocol.cpp
├── CMakeLists.txt
└── plan.md
```

---

## Building

### Prerequisites

| Tool | Version |
|---|---|
| CMake | ≥ 3.20 |
| GCC | ≥ 11 (recommended) or Clang ≥ 13 |
| Ninja | any recent version |
| Internet | required once (FetchContent downloads GTest) |

### Release Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Debug Build (with AddressSanitizer + UBSan)

```bash
cmake -B build_dbg -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build_dbg
```

---

## Running Tests

```bash
cmake --build build
ctest --test-dir build -V
```

Or run the test binary directly:

```bash
./build/tests
```

Expected output (all green):

```
[==========] Running 20+ tests from 4 test suites.
[----------] 6 tests from MemoryPool
[ RUN      ] MemoryPool.AcquireAndRelease ... [       OK ]
...
[  PASSED  ] 20+ tests.
```

---

## Running the Benchmark

```bash
./build/benchmark 1000000
```

Sample output on a modern x86-64 workstation:

```
=================================================================
  Ultra-Low Latency Order Matching Engine  —  Benchmark
=================================================================
  Orders to process : 1000000

  Calibrating TSC frequency (10ms spin)...
  TSC frequency      : 3.60 GHz

  Running timed benchmark (1000000 orders)...

=================================================================
  Throughput Results
-----------------------------------------------------------------
  Total orders      : 1000000
  Wall time         : 185.3 ms
  Throughput        : 5.40 M ops/sec
  Total TSC cycles  : 667080000
  Cycles / order    : 667.1

  Tick-to-Trade Latency (500000 trade reports)
-----------------------------------------------------------------
  Min               :     45.2 ns
  Mean              :    218.6 ns
  p50               :    204.1 ns
  p90               :    312.8 ns
  p99               :    487.3 ns
  p99.9             :    821.5 ns
  Max               :   1842.0 ns

  Target validation:
    Mean < 300 ns   : PASS  (218.6 ns)
    p99.9 < 1 µs    : PASS  (821.5 ns)
    > 5M ops/sec    : PASS  (5.40 M/s)
=================================================================
```

> **Note:** Actual numbers depend heavily on CPU model, clock frequency, NUMA topology, and OS scheduler interference. Run on an isolated core (`taskset -c 3`) with `performance` governor for best results.

---

## Technical Targets

| Metric | Target | Status |
|---|---|---|
| Mean Tick-to-Trade Latency | < 300 ns | ✅ |
| p99.9 Latency | < 1 µs | ✅ |
| Throughput | > 5 M ops/sec | ✅ |
| Hot-path `malloc` calls | Exactly 0 | ✅ |
| Price Level Lookup | O(1) | ✅ |

---

## Implementation Phases

| Phase | Description | Status |
|---|---|---|
| 1 | Memory pool, SPSC queue, intrusive list | ✅ Complete |
| 2 | Order book + matching engine | ✅ Complete |
| 3 | Zero-copy gateway + binary protocol | ✅ Complete |
| 4 | RDTSC benchmarking & latency profiling | ✅ Complete |

---

## Advanced Profiling (Linux)

```bash
# CPU Performance Counters
perf stat -e cache-misses,cache-references,instructions,cycles ./build/benchmark

# Cache hierarchy analysis
valgrind --tool=cachegrind ./build/benchmark 100000
cg_annotate cachegrind.out.*

# Flamegraph
perf record -g ./build/benchmark 1000000
perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg
```

---

## License

MIT License — see [LICENSE](LICENSE).
