#include "exotic/memory/arena.hpp"

#include <algorithm>
#include <cassert>

#include "exotic/memory/tracking_allocator.hpp"

namespace exotic::memory {

Arena::Chunk::~Chunk() {
    if (memory) {
        TrackingAllocator::instance().deallocate(memory);
    }
}

Arena::Arena(std::size_t initial_chunk_bytes) noexcept : next_chunk_bytes_(std::max<std::size_t>(initial_chunk_bytes, 4096)) {}

Arena::~Arena() = default;

Arena::Arena(Arena&& other) noexcept
    : head_(std::move(other.head_)),
      current_(other.current_),
      next_chunk_bytes_(other.next_chunk_bytes_),
      total_allocated_(other.total_allocated_),
      total_reserved_(other.total_reserved_),
      chunk_count_(other.chunk_count_) {
    other.current_ = nullptr;
    other.total_allocated_ = 0;
    other.total_reserved_ = 0;
    other.chunk_count_ = 0;
}

Arena& Arena::operator=(Arena&& other) noexcept {
    if (this != &other) {
        head_ = std::move(other.head_);
        current_ = other.current_;
        next_chunk_bytes_ = other.next_chunk_bytes_;
        total_allocated_ = other.total_allocated_;
        total_reserved_ = other.total_reserved_;
        chunk_count_ = other.chunk_count_;

        other.current_ = nullptr;
        other.total_allocated_ = 0;
        other.total_reserved_ = 0;
        other.chunk_count_ = 0;
    }
    return *this;
}

void Arena::add_chunk(std::size_t min_bytes) {
    const std::size_t capacity = std::max(min_bytes, next_chunk_bytes_);

    auto chunk = std::make_unique<Chunk>();
    chunk->memory = static_cast<std::byte*>(
        TrackingAllocator::instance().allocate(capacity, alignof(std::max_align_t), "Arena::chunk"));
    chunk->capacity = capacity;
    chunk->used = 0;
    chunk->next = std::move(head_);

    head_ = std::move(chunk);
    current_ = head_.get();

    total_reserved_ += capacity;
    ++chunk_count_;
    next_chunk_bytes_ = capacity * 2;
}

void* Arena::allocate(std::size_t size, std::size_t alignment) {
    assert(alignment != 0 && (alignment & (alignment - 1)) == 0 && "alignment must be a power of two");

    if (!current_) {
        add_chunk(size + alignment);
    }

    for (;;) {
        std::byte* base = current_->memory + current_->used;
        std::size_t space = current_->capacity - current_->used;
        void* aligned = base;
        std::size_t avail = space;

        if (void* result = std::align(alignment, size, aligned, avail)) {
            (void)result;
            auto* aligned_bytes = static_cast<std::byte*>(aligned);
            const std::size_t padding = static_cast<std::size_t>(aligned_bytes - base);
            current_->used += padding + size;
            total_allocated_ += size;
            return aligned_bytes;
        }

        add_chunk(size + alignment);
    }
}

void Arena::reset() noexcept {
    if (head_) {
        head_->next.reset();
        head_->used = 0;
        current_ = head_.get();
        total_reserved_ = head_->capacity;
        next_chunk_bytes_ = head_->capacity;
        chunk_count_ = 1;
    } else {
        chunk_count_ = 0;
    }
    total_allocated_ = 0;
}

}  // namespace exotic::memory
