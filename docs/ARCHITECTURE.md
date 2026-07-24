# Architecture

## Design goals

RushGrid prioritizes data flow that is easy to inspect and measure:

1. deterministic fixed-step state changes;
2. contiguous ownership of hot agent data;
3. shared navigation work;
4. bounded local-neighbour work;
5. explicit parallel read/write phases; and
6. visualization that never mutates live state.

The code is a specialized simulation rather than a general-purpose engine or
ECS. That keeps the portfolio-relevant decisions visible.

## Tick pipeline

`Simulation::tick()` owns the phase boundary:

```text
rebuild dirty navigation fields
    ↓
build spatial grid
    ↓
parallel steering
    ↓
parallel integration
    ↓
arrival processing
    ↓
publish render snapshot
```

The navigation phase is skipped unless a gate command changes grid
walkability. Both goal fields are rebuilt completely and become visible before
steering begins, so agents never observe a partially updated field.

The spatial grid reads current positions. Steering reads current positions,
the completed grid, and completed flow fields; each job writes only its
assigned desired-velocity range. Integration reads those desired velocities
and writes next positions plus owned velocity slots. Position buffers are
swapped after the integration fence.

## Determinism

- Simulation time advances by the configured fixed delta (60 Hz by default).
- Spawn state comes from a named `std::mt19937` seed.
- Commands are applied at a tick boundary.
- Spatial scatter order is dense-index order.
- Parallel jobs never reduce shared floating-point values.
- Respawn jitter comes from an integer hash of stable state.
- `state_hash()` quantizes positions to one millimetre and hashes them with
  goal and tick state.

The test suite runs identical 90-tick, gate-closing scenarios with zero and
three workers and requires identical hashes.

## Dynamic gate commands

Closing a gate updates six cells in the fare line and invalidates both flow
fields. Agents occupying a closing cell are moved to the side they were already
traversing toward before the cells become blocked. This command-boundary rule
prevents agents from being stranded in invalid geometry.

## Rendering boundary

Two pre-reserved `RenderSnapshot` instances hold positions, velocities, goals,
and the source tick. A tick writes the unpublished buffer, then releases its
index atomically. The debug-image writer consumes only the published snapshot.

The current app runs simulation and rendering serially. The release/acquire
publication boundary makes the ownership explicit and prepares the data flow
for a dedicated renderer; a future independently paced renderer would add a
consumer acknowledgement or a third buffer so the producer cannot lap it.

## Executables

- `rushgrid_demo`: fixed-length scenario runner, live coarse timings, dynamic
  incident, and PPM snapshot export.
- `rushgrid_benchmark`: reproducible warm-up/capture/repeat runner and CSV
  exporter.
- `rushgrid_tests`: dependency-free unit and integration test runner.
