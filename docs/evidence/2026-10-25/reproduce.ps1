param([string]$OutputDirectory = 'docs/evidence/scaling-rerun')
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new output directory to preserve prior evidence.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
New-Item -ItemType Directory -Force -Path 'build/evidence-oct25' | Out-Null
$sources = @('src/jobs/thread_pool.cpp','src/navigation/navigation.cpp','src/simulation/agent_storage.cpp','src/simulation/simulation.cpp','src/spatial/spatial_grid.cpp','src/telemetry/profiler.cpp','src/telemetry/statistics.cpp','src/visualization/debug_image.cpp')
$flags = @('-std=c++20','-O3','-DNDEBUG','-Wall','-Wextra','-Wpedantic','-Iinclude','-static','-static-libgcc','-static-libstdc++')
g++ --version | Out-File (Join-Path $OutputDirectory 'build-and-test.txt')
g++ @flags @sources src/app/benchmark_main.cpp -o build/evidence-oct25/rushgrid_benchmark.exe 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory 'build-and-test.txt') -Append
if ($LASTEXITCODE -ne 0) { throw 'Benchmark build failed' }
g++ @flags @sources tests/rushgrid_tests.cpp -o build/evidence-oct25/rushgrid_tests.exe 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory 'build-and-test.txt') -Append
if ($LASTEXITCODE -ne 0) { throw 'Regression build failed' }
& './build/evidence-oct25/rushgrid_tests.exe' 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory 'build-and-test.txt') -Append
if ($LASTEXITCODE -ne 0) { throw 'Regression checks failed' }
$commands = @()
foreach ($workers in @(0,5,5,0,0,5,5,0,0,5)) {
    $runArguments = @('--scenario','concourse','--agents','10000','--threads',"$workers",'--implementation','optimized','--seed','1337','--warmup-seconds','1','--duration-seconds','5','--repeat','1','--output',(Join-Path $OutputDirectory 'captures.csv'))
    $started = [DateTime]::UtcNow.ToString('o')
    & './build/evidence-oct25/rushgrid_benchmark.exe' @runArguments
    $runExit = $LASTEXITCODE
    $commands += [PSCustomObject]@{ startedAtUtc = $started; executable = './build/evidence-oct25/rushgrid_benchmark.exe'; arguments = $runArguments; exitCode = $runExit }
    $commands | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'commands.json') -Encoding utf8
    if ($runExit -ne 0) { throw 'Benchmark capture failed; partial evidence retained.' }
}
node docs/evidence/2026-10-25/summarize.mjs $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'Summary validation failed' }
