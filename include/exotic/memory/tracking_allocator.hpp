#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "exotic/core/platform.hpp"

namespace exotic::memory {

// Point-in-time snapshot of allocator activity. Counters are read from
// per-thread shards (see TrackingAllocator) without a global lock, so under
// concurrent allocation this is an approximately-consistent snapshot, not an
// atomic one -- adequate for its purpose (metrics/observability), and it
// never sacrifices correctness of the allocations themselves.
struct AllocationStats {
    std::size_t total_allocated_bytes = 0;
    std::size_t total_freed_bytes = 0;
    std::size_t live_bytes = 0;
    std::size_t total_allocations = 0;
    std::size_t total_frees = 0;
    std::size_t live_allocations = 0;
    std::size_t peak_live_bytes = 0;
};

// Process-wide instrumented allocator: every allocation is tagged with an
// optional label, sized/aligned bookkeeping is stored just ahead of the
// returned pointer (so deallocate() needs no size argument), and running
// totals are kept in per-thread-shard atomics to avoid turning allocation
// into a single-cache-line bottleneck under concurrency.
//
// In debug builds (EXOTIC_DEBUG, i.e. NDEBUG not defined) every live
// allocation's pointer/size/tag is additionally recorded in a mutex-guarded
// map so leak_report() can list everything that was allocated and never
// freed. That bookkeeping is compiled out entirely in release builds -- it
// exists to catch leaks during development/testing, not to run in
// production.
class TrackingAllocator {
public:
    static TrackingAllocator& instance();

    [[nodiscard]] void* allocate(std::size_t size,
                                  std::size_t alignment = alignof(std::max_align_t),
                                  const char* tag = nullptr);
    void deallocate(void* ptr) noexcept;

    template <typename T, typename... Args>
    [[nodiscard]] T* create(const char* tag, Args&&... args) {
        void* mem = allocate(sizeof(T), alignof(T), tag);
        return ::new (mem) T(std::forward<Args>(args)...);
    }

    template <typename T>
    void destroy(T* obj) noexcept {
        if (!obj) return;
        obj->~T();
        deallocate(obj);
    }

    [[nodiscard]] AllocationStats stats() const;

    // Debug-mode only: human-readable listing of every allocation still
    // outstanding (pointer, size, tag). Always returns an empty string in
    // release builds.
    [[nodiscard]] std::string leak_report() const;

    // Test/benchmark hook: clears the debug live-allocation map's memory of
    // anything already freed and resets the sampled peak. Does not affect
    // correctness of an already-running process; intended for isolating
    // test cases from each other.
    void reset_peak_for_testing() noexcept;

private:
    TrackingAllocator() = default;

    struct alignas(core::kCacheLineSize) CounterShard {
        std::atomic<std::size_t> allocated_bytes{0};
        std::atomic<std::size_t> freed_bytes{0};
        std::atomic<std::size_t> allocations{0};
        std::atomic<std::size_t> frees{0};
    };

    static constexpr std::size_t kShardCount = 16;

    CounterShard& shard_for_this_thread() const;

    mutable std::array<CounterShard, kShardCount> shards_{};
    mutable std::atomic<std::size_t> next_shard_{0};
    mutable std::atomic<std::size_t> peak_live_bytes_{0};

#if EXOTIC_DEBUG
    struct DebugRecord {
        std::size_t size;
        const char* tag;
    };
    mutable std::mutex debug_mutex_;
    std::unordered_map<void*, DebugRecord> debug_live_;
#endif
};

}  // namespace exotic::memory
