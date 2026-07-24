# Profiling and reproduction

## Checked-in capture

Date: 2026-07-24

- CPU: AMD Ryzen 5 3600, 6 cores / 12 logical processors
- OS: Microsoft Windows build 26200
- Compiler: GCC 13.2.0
- Build: `-O3 -DNDEBUG`, CMake Release preset
- Scenario: `concourse`
- Seed: 1337
- Fixed step: 60 Hz
- Optimized main matrix: 5 workers
- Warm-up: 60 ticks
- Capture: 300 ticks
- Repeats: 3 independent simulations

The benchmark closes the upper gate halfway through each measured capture. That
tick includes both flow-field rebuilds instead of hiding dynamic navigation
cost outside the reported total.

| Agents | Implementation | Workers | Mean average | Mean p95 |
|---:|---|---:|---:|---:|
| 1,000 | optimized | 5 | 0.131 ms | 0.160 ms |
| 5,000 | optimized | 5 | 0.457 ms | 0.537 ms |
| 10,000 | optimized | 5 | 1.003 ms | 1.185 ms |
| 25,000 | optimized | 5 | 3.418 ms | 4.241 ms |
| 50,000 | optimized | 5 | 10.540 ms | 11.699 ms |
| 1,000 | baseline | 0 | 8.564 ms | 12.777 ms |
| 5,000 | baseline | 0 | 89.715 ms | 109.449 ms |

No result is extrapolated. Larger baseline counts were not included in the
five-second repeated capture because all-pairs work makes them intentionally
slow.

## Worker scaling at 50,000 agents

The scaling rows use 180 measured ticks and three repeats:

| Workers | Effective participants | Mean tick | Speedup vs 0 |
|---:|---:|---:|---:|
| 0 | caller only | 46.912 ms | 1.00× |
| 1 | caller + 1 worker | 30.653 ms | 1.53× |
| 3 | caller + 3 workers | 16.261 ms | 2.88× |
| 5 | caller + 5 workers | 10.540 ms | 4.45× |
| 11 | caller + 11 workers | 8.666 ms | 5.41× |

![Worker scaling](../artifacts/charts/worker_scaling.svg)

## Reproduce the long-form matrix

```powershell
cmake --preset release
cmake --build --preset release

$counts = 1000, 5000, 10000, 25000, 50000
foreach ($count in $counts) {
    .\build\release\rushgrid_benchmark.exe `
      --scenario concourse `
      --agents $count `
      --threads 11 `
      --implementation optimized `
      --seed 1337 `
      --warmup-seconds 5 `
      --duration-seconds 30 `
      --repeat 3 `
      --output artifacts\csv\long_matrix.csv
}
```

Use a quiet machine and avoid running configurations concurrently. Raw rows
contain average, p50, p95, p99, minimum, maximum, per-system averages, updates
per second, arrivals, reserved bytes, and final state hash.

## Interpretation limits

- The checked-in capture is short enough for repository verification, not a
  substitute for a dedicated lab run.
- Process RSS and total allocator activity are not measured.
- Navigation rebuild time is coarse wall-clock timing, not a scoped event trace.
- Baseline and optimized floating-point trajectories are not expected to match;
  determinism is checked within an implementation across worker counts.
- Worker counts include persistent workers only; the waiting caller also helps.
- Results describe this named hardware and compiler only.
