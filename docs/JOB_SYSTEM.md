# Job system

## Worker ownership

`ThreadPool` creates a persistent set of workers. Each cache-line-aligned worker
owns a mutex-protected double-ended queue:

- submissions go to the front in round-robin order;
- an owner takes from its front; and
- a thief takes from the back of another queue.

The caller participates through wait-with-help, stealing queued work until only
already-running jobs remain.

## Parallel-for

`parallel_for()` targets four chunks per worker while respecting a
system-specific minimum grain. RushGrid uses:

- 256 agents per steering chunk; and
- 512 agents per integration chunk.

The function returns only when the global pending counter reaches zero.

## Sleeping without polling

The first implementation used timed condition-variable waits. On Windows, a
missed notification could turn a nominal 1 ms fallback into an approximately
15 ms tick spike.

The verified implementation uses C++20 atomic wait/notify:

- `wakeGeneration` changes for every submission and during shutdown;
- idle workers wait on the observed generation;
- the last completed job notifies waiters on `pending`; and
- the caller waits on the exact pending value after it cannot find queued work.

There is no timeout, polling interval, or lost-notification window. The change
reduced the 5,000-agent optimized p95 from multi-millisecond scheduler spikes
to roughly 0.54 ms in the checked-in capture.

## Race model

The scheduler synchronizes queue ownership, but systems are responsible for
data ownership:

- steering jobs read current positions and write disjoint desired slots;
- integration jobs read desired slots and write disjoint next-position and
  velocity slots;
- all steering jobs finish before any integration job is submitted; and
- buffer publication occurs only after integration completes.

Tests cover exact parallel-for range coverage, repeated shutdown/completion,
and equal state hashes across worker counts. The project was also exercised
through 100 consecutive Release test runs after the atomic-wait change.

## Instrumentation

Atomic counters record jobs executed, jobs stolen, and maximum observed queue
depth. A preallocated scoped profiler records each steering/integration chunk
with its thread lane and publishes a double-buffered event frame. The
interactive viewer renders these events with `T`. Exact worker utilization is
not reduced to one percentage because the caller also executes jobs.
