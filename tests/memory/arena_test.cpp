#include "exotic/memory/arena.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>

using exotic::memory::Arena;
using exotic::memory::ArenaScope;

namespace {
struct Point {
    int x, y;
    Point(int x_, int y_) : x(x_), y(y_) {}
};
}  // namespace

TEST_CASE("Arena allocates aligned, writable memory", "[memory][arena]") {
    Arena arena(1024);
    void* p1 = arena.allocate(16, 8);
    REQUIRE(p1 != nullptr);
    REQUIRE(reinterpret_cast<std::uintptr_t>(p1) % 8 == 0);
    std::memset(p1, 0xAB, 16);

    void* p2 = arena.allocate(32, 16);
    REQUIRE(p2 != nullptr);
    REQUIRE(reinterpret_cast<std::uintptr_t>(p2) % 16 == 0);
    REQUIRE(p1 != p2);
}

TEST_CASE("Arena::create constructs objects usable in place", "[memory][arena]") {
    Arena arena;
    Point* p = arena.create<Point>(3, 4);
    REQUIRE(p->x == 3);
    REQUIRE(p->y == 4);
}

TEST_CASE("Arena grows across chunk boundaries and tracks bytes", "[memory][arena]") {
    Arena arena(64);  // tiny first chunk forces growth quickly
    for (int i = 0; i < 1000; ++i) {
        void* p = arena.allocate(64, 8);
        REQUIRE(p != nullptr);
    }
    REQUIRE(arena.bytes_allocated() == 64 * 1000);
    REQUIRE(arena.bytes_reserved() >= arena.bytes_allocated());
    REQUIRE(arena.chunk_count() > 1);
}

TEST_CASE("Arena::reset reclaims memory and keeps the largest chunk", "[memory][arena]") {
    Arena arena(64);
    for (int i = 0; i < 200; ++i) {
        arena.allocate(64, 8);
    }
    REQUIRE(arena.chunk_count() > 1);

    arena.reset();
    REQUIRE(arena.bytes_allocated() == 0);
    REQUIRE(arena.chunk_count() == 1);

    // Post-reset allocation must still work correctly.
    void* p = arena.allocate(16, 8);
    REQUIRE(p != nullptr);
    REQUIRE(arena.bytes_allocated() == 16);
}

TEST_CASE("ArenaScope resets on scope exit, including via exception", "[memory][arena]") {
    Arena arena(64);
    {
        ArenaScope scope(arena);
        arena.allocate(32, 8);
        REQUIRE(arena.bytes_allocated() == 32);
    }
    REQUIRE(arena.bytes_allocated() == 0);

    bool threw = false;
    try {
        ArenaScope scope(arena);
        arena.allocate(32, 8);
        throw std::runtime_error("boom");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    REQUIRE(threw);
    REQUIRE(arena.bytes_allocated() == 0);
}

TEST_CASE("Arena move construction transfers state", "[memory][arena]") {
    Arena a(64);
    a.allocate(16, 8);
    const std::size_t allocated = a.bytes_allocated();

    Arena b(std::move(a));
    REQUIRE(b.bytes_allocated() == allocated);
    REQUIRE(a.bytes_allocated() == 0);
    REQUIRE(a.chunk_count() == 0);
}
