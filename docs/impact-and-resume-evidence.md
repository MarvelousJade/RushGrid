# RushGrid: impact and resume evidence

Reviewed October 25, 2026. [Source-review fingerprints](evidence/resume-review-2026-10-25.json).

## Verified outcome — October 25, 2026

**Status: verified for this local synthetic experiment. Category: engineering.** Median run-average tick fell from **3.433175 ms** (no persistent workers) to **0.899424 ms** (five workers): **3.8171x speedup / 73.8020% lower time**. Baseline run-average range: 3.312704–3.545741 ms; five-worker range: 0.887973–0.915299 ms. Five independent captures per condition, each with 300 measured ticks after 60 warm-up ticks. Median within-run p95 was 4.2581 versus 1.1457 ms; this is not a pooled p95.

All ten final state hashes, arrival counts and allocated-storage values matched; the current regression executable passed. This is worker-configuration scaling at the same source SHA, not an implemented source-code optimization. GCC 16.1.0, Release flags, Ryzen 5 3600 and full environment are recorded in summary.json.

Proposed project-outcome bullet: “Measured 3.82x worker scaling for a 10,000-agent C++20 simulation (3.43 to 0.90 ms median run-average tick) across five paired local synthetic captures with identical final states.” This measurement was executed automatically in this session; it does not establish personal implementation or manual execution by the candidate.

Reproduce from the repository root using a new output directory:

```powershell
pwsh -NoProfile -File docs/evidence/2026-10-25/reproduce.ps1 -OutputDirectory docs/evidence/scaling-rerun
```

For arithmetic and consistency verification of existing raw captures only:

```powershell
node docs/evidence/2026-10-25/summarize.mjs
```

## Current worker-scaling experiment

See [predeclared protocol](evidence/2026-10-25/protocol.md), [raw captures](evidence/2026-10-25/captures.csv), [commands](evidence/2026-10-25/commands.json), and [summary/environment/source fingerprints](evidence/2026-10-25/summary.json). This compares worker configurations of the same current implementation, not a before/after source change. The summary is the authority for numerical wording, acceptance and all five paired samples.

Problem: update a reproducible 10,000-agent concourse simulation efficiently. Technical approach: persistent work-stealing workers over the existing optimized storage/grid/flow-field implementation. Baseline: optimized implementation, caller only (zero persistent workers); comparison: caller plus five persistent workers. Both source SHAs are identical and recorded in summary.json. This increment measures existing behavior; it does not claim to have implemented those systems.

Workload: seed 1337, 60 warm-up ticks, 300 measured ticks per capture, five independent captures per configuration with pair order alternated. Dynamic gate closure and flow-field rebuild remain inside each capture. Detailed profiler zones off. Warm simulation state; no OS cold-cache guarantee. Correctness requires identical final state hashes and arrival counts across all captures, plus passing current regression executable. Allocated storage bytes are not process RSS.

Timing is wall-clock on the recorded machine, sensitive to scheduling, antivirus, power and other workloads. Run-average medians and run-average ranges summarize repeated captures; median within-run p95 is not a pooled p95. No real-time deadline guarantee, network/backend throughput, production usage, user savings or arbitrary-hardware claim.

Personal contribution: not established by this automated measurement. Quantified project wording is supplied in the summary only if correctness checks pass. Use first-person action verbs only for confirmed work. Safe capability statement: “Provides a deterministic C++20 crowd simulation with spatial partitioning, shared flow fields and parallel jobs.”

## Historical evidence kept separate

[PROFILING.md](PROFILING.md) and [original raw CSV](../artifacts/csv/sample.csv) retain July 24 captures on Ryzen 5 3600, Windows build 26200 and GCC 13.2.0, with three repeats. Do not mix historical timings with the new machine or treat a configuration comparison as a source-code improvement. Optimized/baseline implementation trajectories differ; deterministic equivalence is checked within the optimized implementation across worker counts.

Strongest candidates: measured current worker scaling when equivalence passes; absolute current 10,000-agent tick duration. They describe one experiment, not two independent achievements. Other workflow and real-world impact are unverified.

## Measurement policy for future changes

Choose one or two useful metrics during planning and record acceptance criteria before implementation. Measure the old version first; for new functionality use an initial implementation or a defined manual workflow. Compare equivalent inputs, environments and required outputs. Without a defensible comparator, report an absolute result. Test totals, lines of code and commits are verification/scope evidence, not user impact.

For every experiment record metric/value, rationale, baseline and delivered source commits (or explicitly unknown/not applicable), date, OS, CPU/RAM, tool versions, dataset origin/hash/size and whether synthetic/demo/real, exact commands, repetitions, warm-up and cache state, all raw results, summary statistics and correctness checks. Record limitations, confounders and unfavorable runs. Repeat timings enough to distinguish variation; normally start with at least five paired trials and increase if variation warrants it. Keep cold and warm cache comparisons labeled and separate from code improvements.

Reduction = (baseline - improved) / baseline * 100. Speedup = baseline time / improved time. Use percentage points for differences between rates. Do not average incompatible workloads, mix source revisions, select only favorable samples or turn a reciprocal of latency into sustained throughput.

Separate engineering results (performance/correctness/recovery), workflow results (observed task time/manual steps) and real-world impact (observed adoption/user/business outcomes). Synthetic experiments support engineering claims only. Do not invent hours saved, customers, revenue or production capacity.

Each candidate claim must record problem/baseline, change, measured result, supporting commits/artifacts, conditions/limitations, verified personal contribution, proposed bullet and status **verified**, **preliminary** or **unverified**. Project results do not establish who personally implemented or executed the work. Use first-person implementation verbs only when contribution is independently known; otherwise use the capability wording below. Record confirmed personal work separately.

Before handoff trace every number to evidence, check applicability to the delivered tree, identify the strongest two or three results and mark remaining validation. Prepare draft bullets here; change an actual resume only when requested under the current resume instructions.

### Record template

| Field | Required record |
| --- | --- |
| Problem / value / category | Why this metric matters; engineering, workflow or real-world |
| Baseline / improved revision | Full SHAs; dirty state and source hashes; use N/A if no comparator |
| Environment / date | OS, CPU/RAM, runtime/compiler/dependency versions |
| Dataset | Origin, hash, size, characteristics; synthetic/demo/real |
| Procedure | Exact commands/steps; repetitions; warm-up/cache; condition order |
| Raw evidence / summary | Every sample/artifact; median/range/p95 as appropriate |
| Arithmetic / correctness | Explicit calculation; equivalent required outputs |
| Limitations | Noise, missing metadata, uncontrolled factors, negative results |
| Contribution / bullet / status | Actual personal work, supported wording, validation still needed |
