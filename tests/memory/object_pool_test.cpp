#include "exotic/memory/object_pool.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

using exotic::memory::ObjectPool;

namespace {
struct Widget {
    int value;
    static inline std::atomic<int> live_count{0};
    explicit Widget(int v) : value(v) { live_count.fetch_add(1, std::memory_order_relaxed); }
    ~Widget() { live_count.fetch_sub(1, std::memory_order_relaxed); }
};
}  // namespace

TEST_CASE("ObjectPool acquire/release round-trips values and destructors", "[memory][object_pool]") {
    ObjectPool<Widget> pool;
    Widget* w = pool.acquire(42);
    REQUIRE(w->value == 42);
    REQUIRE(Widget::live_count.load() == 1);

    pool.release(w);
    REQUIRE(Widget::live_count.load() == 0);
}

TEST_CASE("ObjectPool recycles freed slots instead of growing unbounded", "[memory][object_pool]") {
    ObjectPool<Widget> pool;
    Widget* first = pool.acquire(1);
    const auto capacity_after_first = pool.stats().capacity;
    pool.release(first);

    Widget* second = pool.acquire(2);
    // Reused the freed slot rather than claiming a fresh one.
    REQUIRE(pool.stats().capacity == capacity_after_first);
    pool.release(second);
}

TEST_CASE("ObjectPool::acquire_unique releases automatically via RAII", "[memory][object_pool]") {
    ObjectPool<Widget> pool;
    {
        auto w = pool.acquire_unique(7);
        REQUIRE(w->value == 7);
        REQUIRE(Widget::live_count.load() == 1);
    }
    REQUIRE(Widget::live_count.load() == 0);
}

TEST_CASE("ObjectPool stats reflect outstanding vs free", "[memory][object_pool]") {
    ObjectPool<Widget> pool;
    std::vector<Widget*> held;
    for (int i = 0; i < 10; ++i) held.push_back(pool.acquire(i));

    auto stats = pool.stats();
    REQUIRE(stats.outstanding == 10);
    REQUIRE(stats.capacity >= 10);

    for (auto* w : held) pool.release(w);
    stats = pool.stats();
    REQUIRE(stats.outstanding == 0);
}

TEST_CASE("ObjectPool survives concurrent acquire/release from many threads", "[memory][object_pool]") {
    ObjectPool<Widget> pool;
    constexpr int kThreads = 8;
    constexpr int kOpsPerThread = 20000;

    // Catch2's assertion reporting isn't safe to call concurrently from
    // multiple threads, so worker threads record failures into a plain
    // atomic instead of calling REQUIRE directly; the actual assertions run
    // once on the main thread after every worker has joined.
    std::atomic<bool> mismatch{false};

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&pool, &mismatch, t] {
            for (int i = 0; i < kOpsPerThread; ++i) {
                const int expected = t * kOpsPerThread + i;
                Widget* w = pool.acquire(expected);
                if (w->value != expected) mismatch.store(true, std::memory_order_relaxed);
                pool.release(w);
            }
        });
    }
    for (auto& th : threads) th.join();

    REQUIRE_FALSE(mismatch.load());
    REQUIRE(Widget::live_count.load() == 0);
    REQUIRE(pool.stats().outstanding == 0);
}
