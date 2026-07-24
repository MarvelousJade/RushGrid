# Data layout

## Optimized agent storage

`AgentStorage` owns one vector per hot component:

```text
position X/Y       next position X/Y
velocity X/Y       desired velocity X/Y
radius             maximum speed
spatial cell       goal / state
entity ID
```

All vectors reserve the configured maximum agent count before spawning. Dense
indices are the unit of simulation work, which makes each phase a linear walk
over the fields it actually consumes. Steering does not load entity-handle or
rare state data merely to read positions.

The storage currently uses standard contiguous vectors. This provides stable,
portable C++20 behavior and leaves an explicit experiment point for aligned
allocators and SIMD without coupling the rest of the simulation to an allocator.

## Generational handles

External identity is a two-word handle:

```cpp
struct EntityId {
    std::uint32_t index;
    std::uint32_t generation;
};
```

A sparse-to-dense table resolves valid handles. Removal swaps the final dense
agent into the hole, repairs that agent's sparse mapping, increments the removed
generation, and recycles its sparse slot. Stale handles therefore fail
validation even after the slot is reused.

## Spatial layout

The uniform grid uses four pre-sized arrays:

- counts per cell;
- prefix-summed cell offsets;
- temporary scatter cursors; and
- a contiguous dense-agent-index array.

Queries walk a small rectangle of cell ranges. The optimized steering phase
caps the number of examined candidates at 48 per agent to bound worst-case
crowd work. Occupancy still includes every agent and feeds density slowdown.

## Baseline layout

`BaselineAgent` deliberately combines position, velocity, steering state,
radius, speed, and goal in one array-of-structures record. Its steering loop
examines every other record and periodically performs an independently
allocated A* search. Keeping this path intact makes the before/after data
reproducible.

## Measured memory

`Simulation::memory_bytes()` reports reserved agent, grid, baseline, and both
render-snapshot buffers. In the checked-in 50,000-agent optimized capture it
reports 5,215,556 bytes. It does not attempt to include allocator bookkeeping,
thread stacks, profiler event buffers, executable image pages, or temporary
navigation rebuild storage, so it is a component-reservation metric rather than
process RSS.

No per-agent containers grow during a tick. Job submission still uses
`std::function`, so a standard library may allocate small task wrappers; a
future fixed job slab is required before claiming zero total tick allocations.
