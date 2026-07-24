# Navigation and crowd behavior

## Navigation grid

The concourse is a 96 × 54 cell grid with one-metre cells. It contains:

- an outer boundary;
- a central service block;
- a fare line at x = 68;
- upper and lower six-cell controllable gates;
- two platform-furniture obstacles; and
- two destinations on the east side.

Diagonal movement is allowed only when both adjacent cardinal cells are open,
which prevents corner cutting through walls.

## Shared flow fields

Each destination owns a `FlowField`. Rebuild runs reverse Dijkstra from the
goal and stores integrated cost for every reachable cell. A second pass chooses
the lowest-cost neighbour and stores a normalized direction.

Agents sample one direction by position during steering. Two Dijkstra searches
therefore serve every optimized agent, rather than running thousands of
independent paths.

Grid revisions and field source revisions make invalidation observable. Gate
commands mark navigation dirty; the old fields are never modified in place.
The current implementation rebuilds synchronously at the next tick boundary.
Asynchronous build-and-swap is the next scalability step for larger maps.

## Baseline A*

The baseline periodically runs A* per agent and keeps only its next direction.
Each search owns fresh score, parent, and open-set storage. This behavior is
intentionally inefficient and remains in the executable as a reproducible
comparison.

## Steering

Optimized desired velocity combines:

1. the shared navigation direction;
2. personal-space separation from nearby grid candidates;
3. an occupancy-based speed reduction; and
4. a left/right tangent probe when the look-ahead cell is blocked.

Velocity changes are acceleration-limited. Integration tries full movement,
then axis-separated sliding, then stops if neither result is walkable.

This is an explainable crowd approximation, not a physical safety model.
The 48-candidate cap bounds cost in extreme density but can omit some nearby
agents; more rigorous production behavior would select the closest candidates
or use density-pressure fields.
