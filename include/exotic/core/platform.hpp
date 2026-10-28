#pragma once

#include <cstddef>

// Feature-detection shim.
//
// This codebase targets C++20, but not every C++20 standard-library facility
// is available on every toolchain we build with (notably: Apple's shipped
// libc++ as of Xcode 14 has <latch>, <barrier>, <semaphore>, <concepts> and
// <ranges>, but lacks <coroutine>, <stop_token>/std::jthread and
// <source_location>). Rather than hard-depend on those and break the build,
// every subsystem that would otherwise reach for one of them uses the
// hand-rolled equivalent in exotic::core (see stop_token.hpp) and a
// continuation-passing Future/Promise instead of coroutines. These macros
// exist so that a future/alternate toolchain with full support can be
// detected and the shims bypassed without touching call sites.

#if defined(__cpp_lib_jthread)
#define EXOTIC_HAS_STD_JTHREAD 1
#else
#define EXOTIC_HAS_STD_JTHREAD 0
#endif

#if defined(__cpp_lib_coroutine)
#define EXOTIC_HAS_STD_COROUTINE 1
#else
#define EXOTIC_HAS_STD_COROUTINE 0
#endif

#if defined(__cpp_lib_source_location)
#define EXOTIC_HAS_STD_SOURCE_LOCATION 1
#else
#define EXOTIC_HAS_STD_SOURCE_LOCATION 0
#endif

#if defined(__cpp_lib_hardware_interference_size)
#define EXOTIC_HAS_HARDWARE_INTERFERENCE_SIZE 1
#else
#define EXOTIC_HAS_HARDWARE_INTERFERENCE_SIZE 0
#endif

#if !defined(NDEBUG)
#define EXOTIC_DEBUG 1
#else
#define EXOTIC_DEBUG 0
#endif

#if defined(__GNUC__) || defined(__clang__)
#define EXOTIC_LIKELY(x) __builtin_expect(!!(x), 1)
#define EXOTIC_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define EXOTIC_LIKELY(x) (x)
#define EXOTIC_UNLIKELY(x) (x)
#endif

namespace exotic::core {

// Practical destructive-interference size for the platforms we target
// (x86_64 and ARM64 both use 64-byte cache lines in practice). Used for
// alignas() padding to prevent false sharing between hot atomics owned by
// different threads. Not sourced from <new> because
// std::hardware_destructive_interference_size is unavailable on this
// toolchain (see EXOTIC_HAS_HARDWARE_INTERFERENCE_SIZE above).
inline constexpr std::size_t kCacheLineSize = 64;

}  // namespace exotic::core
