#include "exotic/memory/tracking_allocator.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

using exotic::memory::TrackingAllocator;

TEST_CASE("TrackingAllocator allocate/deallocate round trips and updates stats", "[memory][tracking_allocator]") {
    auto& alloc = TrackingAllocator::instance();
    const auto before = alloc.stats();

    void* p = alloc.allocate(128, 32, "test-block");
    REQUIRE(p != nullptr);
    REQUIRE(reinterpret_cast<std::uintptr_t>(p) % 32 == 0);
    std::memset(p, 0x7F, 128);

    const auto mid = alloc.stats();
    REQUIRE(mid.total_allocations == before.total_allocations + 1);
    REQUIRE(mid.total_allocated_bytes == before.total_allocated_bytes + 128);
    REQUIRE(mid.live_allocations == before.live_allocations + 1);

    alloc.deallocate(p);
    const auto after = alloc.stats();
    REQUIRE(after.total_frees == before.total_frees + 1);
    REQUIRE(after.live_allocations == before.live_allocations);
}

TEST_CASE("TrackingAllocator create/destroy runs constructors and destructors", "[memory][tracking_allocator]") {
    struct Payload {
        int a;
        double b;
        Payload(int a_, double b_) : a(a_), b(b_) {}
    };

    auto& alloc = TrackingAllocator::instance();
    Payload* p = alloc.create<Payload>("payload", 5, 2.5);
    REQUIRE(p->a == 5);
    REQUIRE(p->b == 2.5);
    alloc.destroy(p);
}

#if EXOTIC_DEBUG
TEST_CASE("TrackingAllocator::leak_report lists outstanding allocations in debug builds", "[memory][tracking_allocator]") {
    auto& alloc = TrackingAllocator::instance();
    void* p = alloc.allocate(64, 8, "leaked-on-purpose");
    REQUIRE(alloc.leak_report().find("leaked-on-purpose") != std::string::npos);
    alloc.deallocate(p);
    REQUIRE(alloc.leak_report().find("leaked-on-purpose") == std::string::npos);
}
#endif

TEST_CASE("TrackingAllocator handles concurrent allocation from many threads", "[memory][tracking_allocator]") {
    auto& alloc = TrackingAllocator::instance();
    const auto before = alloc.stats();

    constexpr int kThreads = 8;
    constexpr int kOpsPerThread = 5000;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&alloc] {
            for (int i = 0; i < kOpsPerThread; ++i) {
                void* p = alloc.allocate(64, 16, "concurrent");
                alloc.deallocate(p);
            }
        });
    }
    for (auto& th : threads) th.join();

    const auto after = alloc.stats();
    REQUIRE(after.total_allocations == before.total_allocations + kThreads * kOpsPerThread);
    REQUIRE(after.total_frees == before.total_frees + kThreads * kOpsPerThread);
    REQUIRE(after.live_allocations == before.live_allocations);
}
