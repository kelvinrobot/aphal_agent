#include <benchmark/benchmark.h>

#include "exotic/memory/arena.hpp"
#include "exotic/memory/object_pool.hpp"
#include "exotic/memory/tracking_allocator.hpp"

using exotic::memory::Arena;
using exotic::memory::ObjectPool;
using exotic::memory::TrackingAllocator;

namespace {
struct Small {
    std::uint64_t a, b, c, d;
};
}  // namespace

static void BM_Arena_Allocate(benchmark::State& state) {
    Arena arena(1 << 20);
    for (auto _ : state) {
        void* p = arena.allocate(sizeof(Small), alignof(Small));
        benchmark::DoNotOptimize(p);
        if (arena.bytes_allocated() > (1 << 20)) arena.reset();
    }
}
BENCHMARK(BM_Arena_Allocate);

static void BM_RawNewDelete(benchmark::State& state) {
    for (auto _ : state) {
        auto* p = new Small{};
        benchmark::DoNotOptimize(p);
        delete p;
    }
}
BENCHMARK(BM_RawNewDelete);

static void BM_ObjectPool_AcquireRelease(benchmark::State& state) {
    ObjectPool<Small> pool;
    for (auto _ : state) {
        Small* p = pool.acquire();
        benchmark::DoNotOptimize(p);
        pool.release(p);
    }
}
BENCHMARK(BM_ObjectPool_AcquireRelease);

static void BM_ObjectPool_AcquireRelease_MultiThreaded(benchmark::State& state) {
    static ObjectPool<Small> pool;
    for (auto _ : state) {
        Small* p = pool.acquire();
        benchmark::DoNotOptimize(p);
        pool.release(p);
    }
}
BENCHMARK(BM_ObjectPool_AcquireRelease_MultiThreaded)->ThreadRange(1, 8);

static void BM_TrackingAllocator_AllocateDeallocate(benchmark::State& state) {
    auto& alloc = TrackingAllocator::instance();
    for (auto _ : state) {
        void* p = alloc.allocate(sizeof(Small), alignof(Small), "bench");
        benchmark::DoNotOptimize(p);
        alloc.deallocate(p);
    }
}
BENCHMARK(BM_TrackingAllocator_AllocateDeallocate);

BENCHMARK_MAIN();
