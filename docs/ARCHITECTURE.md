# exotic — architecture

A production-grade, from-scratch autonomous multi-agent runtime in C++20: a
lightweight "operating system" for AI agents providing lifecycle management,
scheduling, message passing, memory, planning, coordination, tool execution,
and observability.

## Toolchain constraints that shape this design

Verified against the build machine's toolchain (Apple Clang 14 / Xcode-14
libc++, `x86_64-apple-darwin21.6.0`):

| Facility | Available? |
|---|---|
| `<latch>`, `<barrier>`, `<semaphore>`, `<concepts>`, `<ranges>`, `<span>`, `<bit>`, `<compare>` | Yes |
| `<coroutine>` | **No** |
| `<stop_token>` / `std::jthread` | **No** |
| `<source_location>` | **No** |
| `std::hardware_destructive_interference_size` | **No** |

Consequences for the design:

- **No coroutines.** `Future`/`Promise` (concurrency subsystem) are
  continuation-passing with a CAS-based attach-or-run state machine, not
  `co_await`-based. This is arguably a better fit for a "from scratch"
  runtime anyway — the task graph and futures share the same completion
  machinery instead of routing through the compiler's coroutine transform.
- **No `std::jthread`/`stop_token`.** Worker threads are plain `std::thread`
  with a manually-checked `std::atomic<bool>` stop flag, woken via
  `std::counting_semaphore` release bursts on shutdown.
- **No `hardware_destructive_interference_size`.** `exotic::core::kCacheLineSize`
  (64 bytes) is used for `alignas` padding instead.

## Directory structure

```
exotic/
├── CMakeLists.txt
├── cmake/                        # warnings, sanitizers, FetchContent deps
├── include/exotic/<module>/      # public headers, one directory per subsystem
├── src/exotic/<module>/          # .cpp implementation, private state
├── tests/<module>/               # Catch2 unit tests, mirrors module layout
├── benchmarks/                   # Google Benchmark micro-benchmarks
├── examples/                     # runnable demo programs
└── docs/ARCHITECTURE.md
```

Everything currently builds into one `exotic` static library — namespaces
(`exotic::memory`, `exotic::concurrency`, ...) give the modularity; splitting
into per-subsystem libraries is a mechanical follow-up if stricter
build-time isolation is wanted later.

## Subsystems

1. **Agent Runtime** (`exotic::actor`) — lifecycle, states, supervision. *Not yet built.*
2. **Task Scheduler** (`exotic::concurrency`) — thread pool, work-stealing, futures, DAGs, timers. *Not yet built.*
3. **Actor Communication** (`exotic::messaging`) — mailboxes, pub/sub, request-response. *Not yet built.*
4. **Memory System** (`exotic::memory`) — **built**: `Arena`, `ObjectPool<T>`, `TrackingAllocator`, `RefCounted`/`IntrusivePtr<T>`.
5. **Agent Memory** (`exotic::agent_memory`) — short/long-term, embeddings, episodic. *Not yet built.*
6. **Planning Engine** (`exotic::planning`) — decomposition, DAGs, replanning. *Not yet built.*
7. **Multi-Agent Coordination** (`exotic::coordination`) — registry, blackboard, consensus. *Not yet built.*
8. **Tool Execution Runtime** (`exotic::tools`) — registry, sandboxing, circuit breakers. *Not yet built.*
9. **Observability** (`exotic::observability`) — logging, metrics, tracing. *Not yet built.*

## Ownership & memory management

- **Agents**: owned by the runtime via `unique_ptr<Agent>`; referenced
  elsewhere only by `AgentId`, resolved through the registry.
- **Messages**: `unique_ptr<Message>` for point-to-point mailbox delivery
  (single consumer, ownership transferred on push/pop); `shared_ptr<const
  Message>` only for pub/sub fan-out, where shared multi-reader ownership is
  exactly what `shared_ptr`'s atomic refcount is for.
- **Task/Future state blocks**: `RefCounted` + `IntrusivePtr`, pool-allocated
  — avoids `shared_ptr`'s separate control-block allocation on a path that
  creates/destroys huge numbers of tasks.
- **Arena**: scoped bulk allocation (one planning cycle, one tool
  invocation); no per-object free, reclaimed in bulk via `ArenaScope`.
  **Not thread-safe by design** — one arena per execution context.
- **Object pools**: fixed size-class, lock-free-fast-path pools for
  `Message`/`PlanNode`/task nodes to keep hot paths allocation-free.
- **Rule of thumb**: `unique_ptr`/value types by default; `shared_ptr` or
  intrusive refcounting only where lifetime is genuinely shared across
  concurrent owners.

## Concurrency design

- **ThreadPool**: fixed `std::thread` workers, each owning a Chase-Lev
  work-stealing deque; idle workers park on a `std::counting_semaphore`
  rather than condition_variable+mutex; shutdown = atomic stop flag +
  semaphore release burst.
- **Mailbox**: Vyukov-style intrusive MPSC queue — many producers push, only
  the owning agent's execution consumes, so the consumer side never CASes.
- **EventBus**: subscriber list stored as `shared_ptr<const vector<...>>`,
  swapped atomically on subscribe/unsubscribe — publish path is lock-free reads.
- **TaskGraph**: each node holds an atomic dependency countdown;
  `fetch_sub` on completion, the thread observing zero is the sole enqueuer.
- **Future/Promise**: CAS state machine (`Empty → {ContinuationSet |
  Completed}`) — whichever side loses the race performs the other side's job.
- **Timers**: hierarchical timer wheel; a single wheel-advancing thread never
  runs user callbacks itself, only enqueues expired timers as pool tasks.
- **Agent execution modes**: cooperative = `step()` run-to-completion on a
  pool worker (state-machine-driven yield, no stackful coroutines available);
  thread-based = dedicated `std::thread` for agents that must block.

## Identified race conditions & mitigations

1. **Work-stealing deque** torn top/bottom reads — Chase-Lev's precise
   acquire/release/seq_cst placement.
2. **Mailbox push during termination** — closed-flag CAS'd on the mailbox;
   late pushes route to a dead-letter queue.
3. **DAG double-completion / lost wakeup** — `fetch_sub` countdown,
   single-winner-enqueues-on-zero.
4. **Future attach-vs-complete race** — CAS attach-or-run state machine.
5. **Restart racing in-flight messages** — supervisor CAS's agent to
   SUSPENDED, waits on a per-agent execution-generation counter, then swaps
   instance and redelivers undelivered mail.
6. **Object pool free-list ABA** — tagged head (index + monotonic generation
   counter packed into one 64-bit atomic word), not a bare index CAS. See
   `ObjectPool<T>::TaggedHead` — implemented and tested.
7. **Blackboard concurrent claims** — CAS-based claim ops only, versioned
   entries, never read-modify-write across two operations.
8. **Metrics contention** — per-thread sharded counters, aggregated only on
   read. See `TrackingAllocator::CounterShard` — implemented and tested.
9. **Circuit breaker transitions** — atomic enum + atomic failure count, CAS
   transitions, no lock.
10. **Logging from many agent threads** — per-thread SPSC ring buffer fanning
    into one writer thread; callers never block on I/O.

## Status

Built and tested: **Memory subsystem** (`exotic::memory`) — `Arena`,
`ObjectPool<T>`, `TrackingAllocator`, `RefCounted`/`IntrusivePtr<T>`.
Everything else is designed above but not yet implemented; subsystems are
being built incrementally in the order listed, each with its own unit tests
before moving to the next.
