// Small end-to-end demonstration of the memory subsystem: an Arena scoped
// to one "planning cycle", an ObjectPool recycling per-message objects
// across many cycles, an IntrusivePtr-managed shared node, and a final
// TrackingAllocator report showing that nothing leaked.

#include <cstdio>
#include <string>

#include "exotic/memory/arena.hpp"
#include "exotic/memory/object_pool.hpp"
#include "exotic/memory/ref_counted.hpp"
#include "exotic/memory/tracking_allocator.hpp"

using exotic::memory::Arena;
using exotic::memory::ArenaScope;
using exotic::memory::IntrusivePtr;
using exotic::memory::make_intrusive;
using exotic::memory::ObjectPool;
using exotic::memory::RefCounted;
using exotic::memory::TrackingAllocator;

namespace {

struct PlanStep {
    int id;
    double cost;
    PlanStep(int id_, double cost_) : id(id_), cost(cost_) {}
};

struct Message : RefCounted {
    std::string body;
    explicit Message(std::string b) : body(std::move(b)) {}
};

}  // namespace

int main() {
    Arena planning_arena(4096);

    // One arena-scoped "planning cycle": allocate a handful of steps, use
    // them, then let ArenaScope reclaim everything at once on scope exit.
    for (int cycle = 0; cycle < 3; ++cycle) {
        ArenaScope scope(planning_arena);
        PlanStep* steps[4];
        for (int i = 0; i < 4; ++i) {
            steps[i] = planning_arena.create<PlanStep>(i, 1.5 * i);
        }
        std::printf("cycle %d: allocated %zu bytes across %zu chunk(s), step[3].cost=%.1f\n", cycle,
                    planning_arena.bytes_allocated(), planning_arena.chunk_count(), steps[3]->cost);
    }

    // Message objects recycled through a pool instead of round-tripping
    // through the heap on every send.
    ObjectPool<Message> mailbox_pool;
    for (int i = 0; i < 5; ++i) {
        auto msg = mailbox_pool.acquire_unique("hello #" + std::to_string(i));
        std::printf("delivering: %s\n", msg->body.c_str());
    }
    auto pool_stats = mailbox_pool.stats();
    std::printf("pool capacity=%zu outstanding=%zu\n", pool_stats.capacity, pool_stats.outstanding);

    // A payload shared by two "subscribers" via intrusive refcounting.
    IntrusivePtr<Message> shared = make_intrusive<Message>("broadcast payload");
    IntrusivePtr<Message> subscriber_a = shared;
    IntrusivePtr<Message> subscriber_b = shared;
    std::printf("shared message refcount=%ld\n", shared->ref_count());

    auto stats = TrackingAllocator::instance().stats();
    std::printf(
        "allocator stats: live_bytes=%zu live_allocations=%zu total_allocations=%zu peak_live_bytes=%zu\n",
        stats.live_bytes, stats.live_allocations, stats.total_allocations, stats.peak_live_bytes);

    return 0;
}
