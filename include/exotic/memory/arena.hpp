#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <utility>

namespace exotic::memory {

// Bump-pointer arena allocator for scoped, bulk-lifetime allocations (e.g.
// one planning cycle, one tool invocation, one message-decoding pass).
// Individual allocations are never freed; the whole arena is reclaimed at
// once via reset() (or destruction), which is what makes it fast: no
// per-object bookkeeping, no free-list traversal, allocation is just a
// pointer bump plus an occasional new-chunk fallback.
//
// NOT thread-safe by design. An Arena is meant to be owned by exactly one
// execution context (e.g. a single agent step running on a single worker
// thread). Sharing one across threads requires external synchronization;
// if you need concurrent scoped allocation, give each thread/task its own
// Arena instead of contending on one.
//
// Every pointer returned by allocate()/create() is invalidated by reset()
// or destruction. Objects created with create() are placement-new'd but
// never have their destructor run by the arena (reset() just drops the
// backing memory) -- only use create() for trivially-destructible types,
// or destroy manually before reset().
class Arena {
public:
    explicit Arena(std::size_t initial_chunk_bytes = 64 * 1024) noexcept;
    ~Arena();

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&& other) noexcept;
    Arena& operator=(Arena&& other) noexcept;

    // Allocates `size` bytes aligned to `alignment` (must be a power of two).
    // Falls back to a new chunk if the current one doesn't have room; chunks
    // grow geometrically so the amortized cost stays O(1).
    [[nodiscard]] void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t));

    // Constructs a T in arena storage. See class docs: T's destructor will
    // NOT be run automatically.
    template <typename T, typename... Args>
    [[nodiscard]] T* create(Args&&... args) {
        void* mem = allocate(sizeof(T), alignof(T));
        return ::new (mem) T(std::forward<Args>(args)...);
    }

    // Frees every chunk except the most recently allocated one, which is
    // kept (with its used-bytes counter zeroed) to amortize the next
    // cycle's first allocations. Invalidates all previously returned
    // pointers.
    void reset() noexcept;

    [[nodiscard]] std::size_t bytes_allocated() const noexcept { return total_allocated_; }
    [[nodiscard]] std::size_t bytes_reserved() const noexcept { return total_reserved_; }
    [[nodiscard]] std::size_t chunk_count() const noexcept { return chunk_count_; }

private:
    struct Chunk {
        std::byte* memory = nullptr;
        std::size_t capacity = 0;
        std::size_t used = 0;
        std::unique_ptr<Chunk> next;

        Chunk() = default;
        ~Chunk();
        Chunk(const Chunk&) = delete;
        Chunk& operator=(const Chunk&) = delete;
    };

    void add_chunk(std::size_t min_bytes);

    std::unique_ptr<Chunk> head_;
    Chunk* current_ = nullptr;
    std::size_t next_chunk_bytes_;
    std::size_t total_allocated_ = 0;
    std::size_t total_reserved_ = 0;
    std::size_t chunk_count_ = 0;
};

// RAII helper that resets the arena when it goes out of scope, giving an
// Arena clean scope-bound lifetime (e.g. `ArenaScope _(planning_arena);` at
// the top of a planning cycle) without the owner needing to remember to
// call reset() on every exit path, including exceptions.
class ArenaScope {
public:
    explicit ArenaScope(Arena& arena) noexcept : arena_(arena) {}
    ~ArenaScope() { arena_.reset(); }

    ArenaScope(const ArenaScope&) = delete;
    ArenaScope& operator=(const ArenaScope&) = delete;

private:
    Arena& arena_;
};

}  // namespace exotic::memory
