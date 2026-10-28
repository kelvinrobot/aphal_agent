#include "exotic/memory/ref_counted.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

using exotic::memory::IntrusivePtr;
using exotic::memory::make_intrusive;
using exotic::memory::RefCounted;

namespace {
struct Node : RefCounted {
    static inline std::atomic<int> live_count{0};
    int value;
    explicit Node(int v) : value(v) { live_count.fetch_add(1, std::memory_order_relaxed); }
    ~Node() override { live_count.fetch_sub(1, std::memory_order_relaxed); }
};
}  // namespace

TEST_CASE("IntrusivePtr destroys the object when the last reference drops", "[memory][ref_counted]") {
    REQUIRE(Node::live_count.load() == 0);
    {
        IntrusivePtr<Node> a = make_intrusive<Node>(1);
        REQUIRE(a->ref_count() == 1);
        {
            IntrusivePtr<Node> b = a;
            REQUIRE(a->ref_count() == 2);
            REQUIRE(b->value == 1);
        }
        REQUIRE(a->ref_count() == 1);
        REQUIRE(Node::live_count.load() == 1);
    }
    REQUIRE(Node::live_count.load() == 0);
}

TEST_CASE("IntrusivePtr move transfers ownership without touching the refcount", "[memory][ref_counted]") {
    IntrusivePtr<Node> a = make_intrusive<Node>(2);
    IntrusivePtr<Node> b = std::move(a);
    REQUIRE(a.get() == nullptr);
    REQUIRE(b->ref_count() == 1);
}

TEST_CASE("IntrusivePtr copy-assignment releases the old target", "[memory][ref_counted]") {
    IntrusivePtr<Node> a = make_intrusive<Node>(1);
    IntrusivePtr<Node> b = make_intrusive<Node>(2);
    REQUIRE(Node::live_count.load() == 2);

    b = a;
    REQUIRE(Node::live_count.load() == 1);
    REQUIRE(b->value == 1);
    REQUIRE(a->ref_count() == 2);
}

TEST_CASE("IntrusivePtr survives concurrent copy/destroy from many threads", "[memory][ref_counted]") {
    IntrusivePtr<Node> shared = make_intrusive<Node>(99);
    constexpr int kThreads = 8;
    constexpr int kIterations = 50000;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([shared] {
            for (int i = 0; i < kIterations; ++i) {
                IntrusivePtr<Node> local = shared;
                (void)local->value;
            }
        });
    }
    for (auto& th : threads) th.join();

    REQUIRE(shared->ref_count() == 1);
    REQUIRE(Node::live_count.load() == 1);
}
