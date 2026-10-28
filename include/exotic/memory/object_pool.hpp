#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace exotic::memory {

// Fixed-type object pool with a lock-free acquire/release fast path.
//
// Design:
//   - Storage is a fixed-size array of *slab pointers*
//     (std::array<std::atomic<Slot*>, kMaxSlabs>), each lazily allocated on
//     first use and published with release ordering. Because the array of
//     slab pointers itself never resizes, concurrent readers indexing into
//     it never race with a "container growth" the way they would with e.g.
//     a std::deque or std::vector shared across threads -- only the
//     individual slab pointers need acquire/release, not the array.
//   - Freed objects are pushed onto a lock-free Treiber stack (free_head_)
///    threaded through the slots themselves. The head is tagged with a
//     monotonically incrementing counter packed into the same 64-bit atomic
//     word as the head index, which is the standard mitigation for the
//     stack's classic ABA hazard (thread A reads head=X, gets preempted;
//     threads B/C pop X, free something else, and happen to push a *new*
//     node that is also placed at slot X; A resumes and its CAS on `head==X`
//     spuriously succeeds even though the free list state changed
//     underneath it). Tagging the head means A's CAS also has to match the
//     generation, not just the index, so the stale CAS fails instead.
//   - When the free list is empty, a fresh (never-before-used) slot is
//     claimed via an atomic bump counter instead of going through the free
//     list at all -- first-use slots don't need free-list linking.
//
// Every acquired object MUST be released before the pool is destroyed
// (debug builds assert this). The pool owns the objects' storage, not their
// lifetime past that point -- like any pool/arena allocator, it is a
// programming error to let a pool go out of scope while callers still hold
// objects from it.
template <typename T>
class ObjectPool {
public:
    struct Deleter {
        ObjectPool* pool = nullptr;
        void operator()(T* p) const noexcept {
            if (pool && p) pool->release(p);
        }
    };
    using UniquePtr = std::unique_ptr<T, Deleter>;

    struct Stats {
        std::size_t capacity;      // slots ever backed by memory
        std::size_t outstanding;   // currently acquired
        std::size_t free_count;    // approximate: capacity - outstanding
    };

    ObjectPool() = default;

    ~ObjectPool() {
        assert(outstanding_.load(std::memory_order_relaxed) == 0 &&
               "ObjectPool destroyed with objects still outstanding: release() "
               "every acquired object before the pool is destroyed");
        for (auto& slab_atomic : slabs_) {
            delete[] slab_atomic.load(std::memory_order_relaxed);
        }
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;

    template <typename... Args>
    [[nodiscard]] T* acquire(Args&&... args) {
        Slot* slot = pop_free_slot();
        if (!slot) {
            const std::size_t fresh_index = next_fresh_index_.fetch_add(1, std::memory_order_relaxed);
            slot = &slot_at(fresh_index);
        }
        outstanding_.fetch_add(1, std::memory_order_relaxed);
        return ::new (static_cast<void*>(slot->storage)) T(std::forward<Args>(args)...);
    }

    template <typename... Args>
    [[nodiscard]] UniquePtr acquire_unique(Args&&... args) {
        return UniquePtr(acquire(std::forward<Args>(args)...), Deleter{this});
    }

    void release(T* obj) noexcept {
        if (!obj) return;
        obj->~T();

        // Recover the enclosing Slot from the (now-dead) T* via pointer
        // arithmetic -- `storage` is the last member so this is exactly the
        // classic intrusive "container_of" pattern (Linux kernel, boost
        // intrusive lists) applied to a fixed offset established once at
        // slab-creation time.
        auto* storage_bytes = reinterpret_cast<std::byte*>(obj);
        auto* slot = reinterpret_cast<Slot*>(storage_bytes - offsetof(Slot, storage));

        push_free_slot(*slot);
        outstanding_.fetch_sub(1, std::memory_order_relaxed);
    }

    [[nodiscard]] Stats stats() const noexcept {
        const std::size_t capacity = next_fresh_index_.load(std::memory_order_relaxed);
        const std::size_t outstanding = outstanding_.load(std::memory_order_relaxed);
        return Stats{capacity, outstanding, capacity > outstanding ? capacity - outstanding : 0};
    }

private:
    static constexpr std::uint32_t kNullIndex = std::numeric_limits<std::uint32_t>::max();
    static constexpr std::size_t kSlabCapacity = 1024;
    static constexpr std::size_t kMaxSlabs = 1024;  // ceiling: ~1,048,576 live objects per pool

    struct Slot {
        std::uint32_t self_index = 0;             // fixed at slab creation; used by release() bookkeeping (not required, kept for future diagnostics)
        std::atomic<std::uint32_t> free_next{kNullIndex};  // valid only while linked into the free list
        alignas(T) std::byte storage[sizeof(T)];
    };

    struct TaggedHead {
        std::uint32_t index = kNullIndex;
        std::uint32_t tag = 0;

        friend bool operator==(const TaggedHead& a, const TaggedHead& b) noexcept {
            return a.index == b.index && a.tag == b.tag;
        }
    };
    static_assert(std::atomic<TaggedHead>::is_always_lock_free,
                  "TaggedHead must be lock-free for the free list to actually avoid locking");

    Slot* allocate_slab(std::size_t slab_index) {
        auto* fresh = new Slot[kSlabCapacity];
        const std::size_t base = slab_index * kSlabCapacity;
        for (std::size_t i = 0; i < kSlabCapacity; ++i) {
            fresh[i].self_index = static_cast<std::uint32_t>(base + i);
        }
        Slot* expected = nullptr;
        if (slabs_[slab_index].compare_exchange_strong(
                expected, fresh, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return fresh;
        }
        // Lost the race to publish this slab: another thread's slab wins,
        // ours is redundant.
        delete[] fresh;
        return expected;
    }

    Slot& slot_at(std::size_t index) {
        const std::size_t slab_index = index / kSlabCapacity;
        const std::size_t offset = index % kSlabCapacity;
        if (slab_index >= kMaxSlabs) {
            throw std::length_error("ObjectPool: capacity exhausted");
        }
        Slot* slab = slabs_[slab_index].load(std::memory_order_acquire);
        if (!slab) {
            slab = allocate_slab(slab_index);
        }
        return slab[offset];
    }

    Slot* pop_free_slot() {
        TaggedHead old_head = free_head_.load(std::memory_order_acquire);
        while (old_head.index != kNullIndex) {
            Slot& slot = slot_at(old_head.index);
            TaggedHead new_head{slot.free_next.load(std::memory_order_relaxed), old_head.tag + 1};
            if (free_head_.compare_exchange_weak(
                    old_head, new_head, std::memory_order_acq_rel, std::memory_order_acquire)) {
                return &slot;
            }
            // old_head was refreshed with the current value by the failed CAS; retry.
        }
        return nullptr;
    }

    void push_free_slot(Slot& slot) noexcept {
        TaggedHead old_head = free_head_.load(std::memory_order_relaxed);
        TaggedHead new_head;
        do {
            slot.free_next.store(old_head.index, std::memory_order_relaxed);
            new_head = TaggedHead{slot.self_index, old_head.tag + 1};
        } while (!free_head_.compare_exchange_weak(
            old_head, new_head, std::memory_order_acq_rel, std::memory_order_relaxed));
    }

    std::array<std::atomic<Slot*>, kMaxSlabs> slabs_{};
    std::atomic<std::size_t> next_fresh_index_{0};
    std::atomic<TaggedHead> free_head_{};
    std::atomic<std::size_t> outstanding_{0};
};

}  // namespace exotic::memory
