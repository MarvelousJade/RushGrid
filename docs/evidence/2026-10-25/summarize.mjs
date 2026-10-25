import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { cpus, totalmem, release } from 'node:os';

// Run from RushGrid root after the preplanned captures. Dependency-free arithmetic/consistency audit.
const folder = process.argv[2] ?? 'docs/evidence/2026-10-25';
const sha = value => createHash('sha256').update(value).digest('hex');
const git = (...args) => execFileSync('git', args, { encoding: 'utf8' }).trim();
const raw = readFileSync(`${folder}/captures.csv`, 'utf8');
const lines = raw.trim().split(/\r?\n/);
const columns = lines.shift().split(',');
const rows = lines.map(line => Object.fromEntries(line.split(',').map((value, index) => [columns[index], value])));
const commands = JSON.parse(readFileSync(`${folder}/commands.json`, 'utf8').replace(/^\uFEFF/, ''));
assert.equal(rows.length, 10);
assert.equal(commands.length, 10);
for (let i = 0; i < rows.length; i++) {
  const row = rows[i];
  assert.equal(commands[i].exitCode, 0);
  assert.equal(row.workers, commands[i].arguments[commands[i].arguments.indexOf('--threads') + 1]);
  for (const [key, value] of Object.entries({ agents: '10000', seed: '1337', warmup_ticks: '60',
    measured_ticks: '300', implementation: 'optimized', detailed_profiling: 'false', scenario: 'concourse' })) {
    assert.equal(row[key], value);
  }
}
assert.equal(new Set(rows.map(row => row.state_hash)).size, 1, 'Final states must agree');
assert.equal(new Set(rows.map(row => row.total_arrivals)).size, 1, 'Arrival counts must agree');
assert.equal(new Set(rows.map(row => row.memory_bytes)).size, 1, 'Allocated storage must agree');
const median = values => [...values].sort((a, b) => a - b)[Math.floor(values.length / 2)];
function condition(workers) {
  const selected = rows.filter(row => Number(row.workers) === workers);
  assert.equal(selected.length, 5);
  const means = selected.map(row => Number(row.average_ms));
  const p95s = selected.map(row => Number(row.p95_ms));
  assert(means.every(value => Number.isFinite(value) && value > 0));
  return { workers, samples: 5, runMeansMs: means, runP95sMs: p95s,
    medianRunMeanMs: median(means), minRunMeanMs: Math.min(...means), maxRunMeanMs: Math.max(...means),
    medianWithinRunP95Ms: median(p95s) };
}
const baseline = condition(0), comparison = condition(5);
const reduction = (baseline.medianRunMeanMs - comparison.medianRunMeanMs) / baseline.medianRunMeanMs * 100;
const speedup = baseline.medianRunMeanMs / comparison.medianRunMeanMs;
const trackedSources = git('ls-files', 'src', 'include', 'tests', 'CMakeLists.txt').split(/\r?\n/).filter(Boolean);
assert.equal(git('diff', 'HEAD', '--', 'src', 'include', 'tests', 'CMakeLists.txt'), '', 'Measured tree must match HEAD');
const sourceHashes = trackedSources.map(path => ({ path, sha256: sha(readFileSync(path)) }));
const summary = {
  reviewedAtUtc: new Date().toISOString(), status: 'verified bounded local synthetic worker-configuration experiment',
  measuredAtUtc: { start: commands[0].startedAtUtc, lastCaptureStart: commands.at(-1).startedAtUtc },
  baselineSourceCommit: git('rev-parse', 'HEAD'), comparisonSourceCommit: git('rev-parse', 'HEAD'),
  sourceChange: false, baseline, comparison, reductionPercent: reduction, speedup,
  formulas: { reduction: '(baseline median run mean - comparison median run mean) / baseline median run mean * 100',
    speedup: 'baseline median run mean / comparison median run mean' },
  environment: { platform: process.platform, osRelease: release(), cpu: cpus()[0].model,
    logicalCpus: cpus().length, totalMemoryBytes: totalmem(), node: process.version,
    compiler: execFileSync('g++', ['--version'], { encoding: 'utf8' }).split(/\r?\n/)[0],
    compileFlags: '-std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic -Iinclude -static -static-libgcc -static-libstdc++' },
  workload: { origin: 'repository deterministic concourse scenario', synthetic: true, seed: 1337,
    agents: 10000, warmupTicks: 60, measuredTicksPerCapture: 300, repeatsPerCondition: 5,
    conditionOrder: rows.map(row => Number(row.workers)), dynamicGateClosure: 'halfway through each capture',
    cache: 'warm simulation state; no OS cold-cache guarantee; five independent process captures per condition' },
  correctness: { finalStateHash: rows[0].state_hash, allFinalStatesEqual: true,
    totalArrivals: rows[0].total_arrivals, allArrivalCountsEqual: true,
    allocatedStorageBytes: rows[0].memory_bytes, regression: '8/8 current regression groups passed; see build-and-test.txt' },
  rawCsvSha256: sha(raw), commandsSha256: sha(readFileSync(`${folder}/commands.json`)), sourceHashes,
  binaries: ['rushgrid_benchmark.exe', 'rushgrid_tests.exe'].map(name => ({ name,
    sha256: sha(readFileSync(`build/evidence-oct25/${name}`)) })),
  limitations: ['Short local capture; no real usage, process RSS or hard realtime guarantee.',
    'One scenario/seed and machine; no general hardware or workload scaling guarantee.',
    'Raw harness retains run aggregates, not individual tick samples; median of run p95s is not pooled p95.',
    'Background activity, power settings and CPU scheduling were not instrumented.',
    'Existing worker configuration comparison; no source revision improvement or new implementation claimed.',
    'Automated project measurement does not establish the candidate personally implemented or ran it.'],
  candidateOutcomeBullet: `Measured ${speedup.toFixed(2)}x worker scaling for a 10,000-agent C++20 simulation (${baseline.medianRunMeanMs.toFixed(2)} to ${comparison.medianRunMeanMs.toFixed(2)} ms median run-average tick) across five paired local synthetic captures with identical final states.`,
};
writeFileSync(`${folder}/summary.json`, JSON.stringify(summary, null, 2) + '\n');
console.log(JSON.stringify({ baseline: baseline.medianRunMeanMs, comparison: comparison.medianRunMeanMs,
  reductionPercent: reduction, speedup, cpu: summary.environment.cpu, bullet: summary.candidateOutcomeBullet }));
