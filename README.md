# exotic

A C++20 runtime for autonomous multi-agent systems. The project is designed as a lightweight agent operating environment with memory management, scheduling, coordination primitives, and a modular architecture that can be built out incrementally.

## Overview

`exotic` is being developed as a from-scratch runtime for AI agents, with a focus on:

- agent lifecycle and runtime orchestration
- task scheduling and execution
- message passing and coordination
- memory management and allocation strategy
- observability, planning, and tool execution interfaces

The current implementation is centered on the memory subsystem, which is already built and tested.

## Current status

The project is under active incremental development. At the moment, the following are implemented and validated:

- `exotic::memory::Arena`
- `exotic::memory::ObjectPool<T>`
- `exotic::memory::TrackingAllocator`
- `exotic::memory::RefCounted` and `IntrusivePtr<T>`

The broader runtime subsystems described in the architecture are planned extensions, not yet fully implemented.

## Repository layout

```text
.
├── CMakeLists.txt
├── cmake/
├── docs/
├── examples/
├── include/
├── src/
├── tests/
├── benchmarks/
├── build/
├── build-release/
├── build-tsan/
└── README.md
```

## Build

This project uses CMake.

### Configure

```bash
cmake -S . -B build \
  -DEXOTIC_BUILD_TESTS=ON \
  -DEXOTIC_BUILD_BENCHMARKS=ON \
  -DEXOTIC_BUILD_EXAMPLES=ON
```

### Build

```bash
cmake --build build
```

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

### Optional sanitizer build

```bash
cmake -S . -B build-tsan \
  -DEXOTIC_SANITIZER=address
cmake --build build-tsan
```

## Example programs

Examples are located under the `examples/` directory. The project includes a memory demo and can be built with the example option enabled.

```bash
./build/examples/exotic_memory_demo
```

## Documentation

For the design goals, toolchain constraints, runtime architecture, and subsystem roadmap, see:

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)

## License

This project does not currently declare a license file. Add a license before public release or distribution if you intend to publish the code externally.

## Notes

`exotic` is intentionally built with modern C++20 and avoids some newer standard facilities, such as coroutines and `std::jthread`, in favor of a custom runtime architecture. This is a deliberate design choice described in the architecture documentation.
