# Ultra-Low Latency Order Matching Engine & Market Data Gateway

## Problem Statement
In high-frequency trading (HFT) and electronic financial exchanges, trade execution speeds are measured in nanoseconds. Standard software engineering practices—such as dynamic heap memory allocation (`malloc`/`new`), thread synchronization using mutex locks, pointer-heavy abstractions, and deep class hierarchies—introduce non-deterministic execution delays. 

When an order arrives, CPU cache misses, OS context switches, lock contention, and dynamic allocation fragmentation create tail latency spikes (e.g., p99.9 latency). These spikes cause order execution delays, resulting in adverse selection and lost trading opportunities. Building a low-latency execution system requires fundamentally redesigning software to align with underlying CPU architecture, hardware cache line behavior, and system-level memory operations.

## Project Objective
Develop a single-threaded core, zero-allocation Limit Order Book (LOB) matching engine and zero-copy market data parser in C++20. The system must process binary order feeds, maintain price-time priority, match incoming buy and sell orders, and emit trade execution reports with a deterministic median tick-to-trade latency under 300 nanoseconds without allocating memory on the critical execution path.

---

## System Design Concepts

### 1. Zero-Allocation Hot Path & Memory Management
* **Memory Pools & Slab Allocators:** Pre-allocate all object memory at application startup into contiguous memory blocks. Runtime allocations dynamically retrieve slots from fixed-size pools in O(1) time without dynamic OS calls.
* **Intrusive Data Structures:** Nodes store their own tracking pointers (`next`, `prev`) internally rather than using external wrapper pointers. This eliminates separate heap node allocations and improves CPU cache locality.

### 2. Lock-Free Concurrent Architecture
* **Single-Producer Single-Consumer (SPSC) Ring Buffers:** Thread-safe communication between network threads and the core matching thread using lock-free ring buffers backed by atomic operations and explicit memory barriers (`std::memory_order_acquire`, `std::memory_order_release`).
* **Hardware Cache-Line Alignment:** Core structures are aligned to 64-byte boundaries using `alignas(64)` to eliminate false sharing between threads running on adjacent physical CPU cores.

### 3. Cache-Conscious Data Structures
* **Flat Array Limit Levels:** Price levels implemented using dense contiguous memory arrays instead of pointer-based red-black trees (`std::map`), minimizing L1/L2 cache misses during order book depth traversals.
* **Intrusive Doubly Linked Lists:** Orders within the same price level are linked using intrusive doubly linked lists, ensuring fast O(1) order insertion, execution, and cancellation.

### 4. Zero-Copy Protocol Parsing
* **Direct Packet Parsing:** Parse network byte streams by reinterpreting memory buffers directly into C++ POD (Plain Old Data) structs without copying string buffers or creating intermediate objects.
* **Branch Optimization:** Standardize parsing logic using compiler optimization hints (`[[likely]]`, `[[unlikely]]`), bitwise operations, and compile-time template evaluation to minimize runtime CPU branch mispredictions.

---

## Probable Technologies Used

* **Core Language:** C++20 / C++23
* **Build System & Toolchain:** CMake (3.20+), Ninja, GCC 11+ / Clang 13+ with flags (`-O3 -march=native -ffast-math -flto`)
* **Profiling & Benchmarking Infrastructure:**
  * `x86_64` CPU instruction `rdtsc` / `rdtp` for cycle-accurate hardware timestamping.
  * Linux `perf` utility for tracking Instructions Per Cycle (IPC), L1/L3 cache miss ratios, and branch mispredictions.
  * Google Benchmark for isolated unit micro-benchmarking.
  * Valgrind / Cachegrind for memory correctness and cache hierarchy analysis.
* **Testing Framework:** GoogleTest (GTest) for functional correctness and order-matching edge case verification.

---

## Detailed Implementation Plan

### Phase 1: Core Memory Infrastructure & Primitives
* Implement a static fixed-capacity `MemoryPool<T, Capacity>` allocator.
* Build a lock-free Single-Producer Single-Consumer (`SPSCQueue<T, Capacity>`) ring buffer using atomic operations.
* Construct an intrusive doubly linked list (`IntrusiveList<T>`) supporting O(1) insertion and removal.

### Phase 2: Matching Engine & Order Book Logic
* Define binary layout structs: `Order`, `LimitLevel`, `TradeReport`, and `OrderBook`.
* Implement price-time priority matching logic for `ADD`, `CANCEL`, `MODIFY`, and `EXECUTE` commands.
* Design price level structures optimized for rapid top-of-book evaluation (`Best Bid` and `Best Offer`).

### Phase 3: Zero-Copy Network & IPC Interface
* Define a packed binary wire protocol for incoming order requests.
* Implement a zero-copy binary decoder that reinterprets network or shared memory socket byte streams directly into domain structures.
* Connect the gateway receiver thread to the execution core via the lock-free SPSC ring buffer.

### Phase 4: Benchmarking & Low-Level Optimization
* Instrument execution paths with `rdtsc` to collect per-order tick-to-trade latency measurements.
* Generate latency distribution reports (p50, p90, p99, p99.9) under synthetic market data loads (1,000,000 to 10,000,000 ops/sec).
* Profile hardware performance using Linux `perf` and apply cache line alignment, branchless conditional logic, or loop unrolling to optimize system throughput.

---

## Technical Targets & Validation Metrics

| Metric | Target Specification |
| :--- | :--- |
| **Mean Tick-to-Trade Latency** | < 300 nanoseconds |
| **99.9th Percentile (p99.9) Latency** | < 1 microsecond |
| **Throughput Capacity** | > 5,000,000 operations / second |
| **Dynamic Allocations (Hot Path)** | Exactly 0 `malloc`/`new` calls after startup |
| **Order Book Lookup Overhead** | O(1) step operations |