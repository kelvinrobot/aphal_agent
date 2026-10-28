#include "exotic/memory/tracking_allocator.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <new>
#include <sstream>

namespace exotic::memory {

namespace {

// Bookkeeping header stored immediately before the pointer handed back to
// the caller. `block_offset` is the distance back to the raw block returned
// by ::operator new, which may differ from sizeof(Header) once alignment
// padding is taken into account -- that's what lets deallocate() find the
// original block without the caller passing size/alignment back in.
struct Header {
    std::size_t block_offset;
    std::size_t size;
};

static_assert(sizeof(Header) % alignof(std::max_align_t) == 0 ||
                  alignof(std::max_align_t) % sizeof(Header) == 0,
              "Header size must divide evenly with common alignments so "
              "placing it directly before an aligned pointer keeps that "
              "pointer's alignment intact.");

}  // namespace

TrackingAllocator& TrackingAllocator::instance() {
    static TrackingAllocator allocator;
    return allocator;
}

TrackingAllocator::CounterShard& TrackingAllocator::shard_for_this_thread() const {
    thread_local const std::size_t index = next_shard_.fetch_add(1, std::memory_order_relaxed) % kShardCount;
    return shards_[index];
}

void* TrackingAllocator::allocate(std::size_t size, std::size_t alignment, const char* tag) {
    alignment = std::max(alignment, alignof(Header));

    // Reserve room for the header, plus up to (alignment - 1) bytes of
    // padding so std::align always has room to satisfy the caller's
    // alignment for the user-visible pointer.
    const std::size_t header_reserve = sizeof(Header);
    const std::size_t worst_case = header_reserve + alignment - 1 + size;

    void* raw = ::operator new(worst_case);
    auto* raw_bytes = static_cast<std::byte*>(raw);

    void* aligned = raw_bytes + header_reserve;
    std::size_t space = worst_case - header_reserve;
    void* result = std::align(alignment, size, aligned, space);
    assert(result != nullptr && "worst_case reservation guarantees alignment succeeds");
    (void)result;

    auto* user_ptr = static_cast<std::byte*>(aligned);
    auto* header = reinterpret_cast<Header*>(user_ptr) - 1;
    header->block_offset = static_cast<std::size_t>(user_ptr - raw_bytes);
    header->size = size;

    auto& shard = shard_for_this_thread();
    shard.allocated_bytes.fetch_add(size, std::memory_order_relaxed);
    shard.allocations.fetch_add(1, std::memory_order_relaxed);

#if EXOTIC_DEBUG
    {
        std::lock_guard lock(debug_mutex_);
        debug_live_.emplace(static_cast<void*>(user_ptr), DebugRecord{size, tag});
    }
#else
    (void)tag;
#endif

    return user_ptr;
}

void TrackingAllocator::deallocate(void* ptr) noexcept {
    if (!ptr) return;

    auto* user_ptr = static_cast<std::byte*>(ptr);
    auto* header = reinterpret_cast<Header*>(ptr) - 1;
    const std::size_t size = header->size;
    std::byte* raw = user_ptr - header->block_offset;

    auto& shard = shard_for_this_thread();
    shard.freed_bytes.fetch_add(size, std::memory_order_relaxed);
    shard.frees.fetch_add(1, std::memory_order_relaxed);

#if EXOTIC_DEBUG
    {
        std::lock_guard lock(debug_mutex_);
        debug_live_.erase(ptr);
    }
#endif

    ::operator delete(raw);
}

AllocationStats TrackingAllocator::stats() const {
    AllocationStats result{};
    for (const auto& shard : shards_) {
        result.total_allocated_bytes += shard.allocated_bytes.load(std::memory_order_relaxed);
        result.total_freed_bytes += shard.freed_bytes.load(std::memory_order_relaxed);
        result.total_allocations += shard.allocations.load(std::memory_order_relaxed);
        result.total_frees += shard.frees.load(std::memory_order_relaxed);
    }
    result.live_bytes = result.total_allocated_bytes - result.total_freed_bytes;
    result.live_allocations = result.total_allocations - result.total_frees;

    // Sampled peak: updated opportunistically whenever stats() is polled,
    // rather than on every allocation (which would force a full shard sum
    // on the hot path). This trades exactness for zero added allocator
    // contention -- the same tradeoff typical metrics-scraping systems make.
    std::size_t observed_peak = peak_live_bytes_.load(std::memory_order_relaxed);
    while (result.live_bytes > observed_peak &&
           !peak_live_bytes_.compare_exchange_weak(
               observed_peak, result.live_bytes, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
    result.peak_live_bytes = std::max(observed_peak, result.live_bytes);

    return result;
}

std::string TrackingAllocator::leak_report() const {
#if EXOTIC_DEBUG
    std::lock_guard lock(debug_mutex_);
    if (debug_live_.empty()) return {};

    std::ostringstream out;
    out << debug_live_.size() << " outstanding allocation(s):\n";
    for (const auto& [ptr, record] : debug_live_) {
        out << "  " << ptr << "  size=" << record.size << "  tag="
            << (record.tag ? record.tag : "<untagged>") << '\n';
    }
    return out.str();
#else
    return {};
#endif
}

void TrackingAllocator::reset_peak_for_testing() noexcept {
    peak_live_bytes_.store(0, std::memory_order_relaxed);
}

}  // namespace exotic::memory
