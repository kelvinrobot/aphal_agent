#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <utility>

namespace exotic::memory {

// Base class for intrusive reference counting. Prefer this + IntrusivePtr
// over std::shared_ptr for hot-path, high-churn objects (task/future state
// blocks, DAG nodes): it avoids shared_ptr's separate control-block
// allocation and lets the object be pool-allocated as a single block.
// Use std::shared_ptr instead where ownership is genuinely ad hoc/external
// (e.g. pub/sub message payloads handed to arbitrary subscribers).
class RefCounted {
public:
    RefCounted() noexcept = default;
    virtual ~RefCounted() = default;

    RefCounted(const RefCounted&) = delete;
    RefCounted& operator=(const RefCounted&) = delete;

    void add_ref() const noexcept { ref_count_.fetch_add(1, std::memory_order_relaxed); }

    // Returns true if this decrement dropped the count to zero (i.e. the
    // caller now owns the last reference and must destroy the object).
    //
    // The decrement itself only needs release ordering: it must not be
    // reordered before any prior access to the object made by this thread.
    // Only the *final* release additionally needs to synchronize-with every
    // other thread's release, which is what the acquire fence -- taken only
    // on the branch that actually observed the count reaching zero -- does.
    // This is the standard boost::intrusive_ptr / shared_ptr refcount
    // pattern; taking acq_rel on every decrement would pay that
    // synchronization cost on every release, not just the last one.
    [[nodiscard]] bool release_ref() const noexcept {
        if (ref_count_.fetch_sub(1, std::memory_order_release) == 1) {
            std::atomic_thread_fence(std::memory_order_acquire);
            return true;
        }
        return false;
    }

    [[nodiscard]] long ref_count() const noexcept { return ref_count_.load(std::memory_order_relaxed); }

private:
    mutable std::atomic<long> ref_count_{0};
};

template <typename T>
concept RefCountedType = std::derived_from<T, RefCounted>;

// Intrusive smart pointer for RefCounted-derived types. Semantics mirror
// boost::intrusive_ptr: constructing from a raw pointer assumes the object
// does not yet carry a reference on this IntrusivePtr's behalf (add_ref
// defaults to true), matching a bare `new T(...)`.
template <RefCountedType T>
class IntrusivePtr {
public:
    IntrusivePtr() noexcept = default;
    IntrusivePtr(std::nullptr_t) noexcept {}

    explicit IntrusivePtr(T* ptr, bool add_ref = true) noexcept : ptr_(ptr) {
        if (ptr_ && add_ref) ptr_->add_ref();
    }

    IntrusivePtr(const IntrusivePtr& other) noexcept : ptr_(other.ptr_) {
        if (ptr_) ptr_->add_ref();
    }

    IntrusivePtr(IntrusivePtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }

    template <typename U>
        requires std::convertible_to<U*, T*>
    IntrusivePtr(const IntrusivePtr<U>& other) noexcept : ptr_(other.get()) {
        if (ptr_) ptr_->add_ref();
    }

    ~IntrusivePtr() { reset(); }

    IntrusivePtr& operator=(const IntrusivePtr& other) noexcept {
        if (this != &other) {
            IntrusivePtr tmp(other);
            swap(tmp);
        }
        return *this;
    }

    IntrusivePtr& operator=(IntrusivePtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    IntrusivePtr& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }

    void reset() noexcept {
        if (ptr_ && ptr_->release_ref()) {
            delete ptr_;
        }
        ptr_ = nullptr;
    }

    void swap(IntrusivePtr& other) noexcept { std::swap(ptr_, other.ptr_); }

    [[nodiscard]] T* get() const noexcept { return ptr_; }
    [[nodiscard]] T& operator*() const noexcept { return *ptr_; }
    [[nodiscard]] T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    friend bool operator==(const IntrusivePtr& a, const IntrusivePtr& b) noexcept { return a.ptr_ == b.ptr_; }
    friend bool operator==(const IntrusivePtr& a, std::nullptr_t) noexcept { return a.ptr_ == nullptr; }

private:
    T* ptr_ = nullptr;
};

template <RefCountedType T, typename... Args>
[[nodiscard]] IntrusivePtr<T> make_intrusive(Args&&... args) {
    return IntrusivePtr<T>(new T(std::forward<Args>(args)...), /*add_ref=*/true);
}

}  // namespace exotic::memory
