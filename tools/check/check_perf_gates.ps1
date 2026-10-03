<#
.SYNOPSIS
  Render-throughput time gate for SPEC.NF.PERF.02: drives borealis_bench and judges it
  against tools/check/perf_baseline.json.

.DESCRIPTION
  Runs the bench -Repeat times (independent processes), takes the MEDIAN of each metric,
  then applies two kinds of lines from the baseline file:
    - absolute: SPEC.NF.PERF.02's own numbers (cat >= 45 fps, full redraw <= 100 ms)
    - relative: the same requirement's "regression > 10% is FAIL", against the recorded
      reference. Only metrics flagged relative=true get it; the baseline note explains why
      full_redraw_ms is excluded (its capture spread is wider than the 10% window).
  Median aggregation is what makes the tail metrics (p95) usable: single-run tail readings
  swing by tens of percent on background load.

  Important conventions:
  - Time-class gates are NOT in CTest: the numbers depend on machine load, compiler and
    background processes, so an assertion inside the test runner would read red on a busy
    machine. This script is the local/CI trend check.
  - The bench must run on an OPTIMIZED build (cmake --preset msvc-bench -> build-bench/).
    A Debug build inflates the write chain by 20-30x, so its readings describe the
    compiler flags rather than the product. Both the bench JSON and the baseline file carry
    a build_config marker and a mismatch is a hard FAIL -- comparing a Debug reading against
    an optimized reference (or the other way round) is meaningless.
  - The grid is a marker of the same kind: the bench writes its actual rows x columns into the
    JSON and the baseline records the grid it was captured at, and a mismatch is a hard FAIL.
    Frame cost is per cell, so a grid change (a font that resolves differently, a window that
    sizes differently) silently rescales every reading.
  - Idle frames are already excluded by the bench, and the benchmark measures the cost of
    the presentation layer itself (dirty-row filtering, run splitting, colour resolution,
    cursor three-pass), not how fast the far end can produce bytes.
  - cat_mb_per_s (ingestion throughput) is gated as a regression guard, although
    SPEC.NF.PERF.02 does not name a number for it: a slower producer would simply yield
    fewer frames, which the frame-time gates cannot see. chain_mb_per_s and
    ingest_mb_per_s stay ungated -- they are --write-side attribution rungs, and moving
    one usually moves its neighbour.

.PARAMETER BuildDir
  Build directory containing tools/borealis_bench.exe (default "build-bench", i.e. the
  optimized msvc-bench preset).

.PARAMETER Exe
  Explicit bench executable; overrides the BuildDir-derived path.

.PARAMETER Repeat
  Number of independent bench processes to sample; the median wins (default 3).

.PARAMETER Json
  Judge an existing bench JSON instead of running anything (one sample). Mutually exclusive
  with -Repeat.

.PARAMETER Baseline
  Baseline file to judge against (default perf_baseline.json next to this script).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools/check/check_perf_gates.ps1

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools/check/check_perf_gates.ps1 -Repeat 5

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools/check/check_perf_gates.ps1 -Json build-bench/bench.json
#>
param(
    [string]$BuildDir = "build-bench",
    [string]$Exe = "",
    [string]$Json = "",
    [int]$Repeat = 3,
    [string]$Baseline = ""
)

$ErrorActionPreference = 'Stop'

function Get-Median([double[]]$Values) {
    $sorted = $Values | Sort-Object
    $n = $sorted.Count
    if ($n -eq 0) { return [double]::NaN }
    if ($n % 2 -eq 1) { return [double]$sorted[[int](($n - 1) / 2)] }
    return (($sorted[$n / 2 - 1] + $sorted[$n / 2]) / 2.0)
}

if ([string]::IsNullOrWhiteSpace($Baseline)) {
    $Baseline = Join-Path $PSScriptRoot 'perf_baseline.json'
}
if (-not (Test-Path $Baseline)) {
    Write-Host "FAIL  baseline file not found: $Baseline"
    exit 1
}
$cfg = Get-Content $Baseline -Raw | ConvertFrom-Json
$regress = [double]$cfg.max_regression_percent
Write-Host "Baseline: $Baseline ($($cfg.gates.Count) gates, relative window $regress%)"

# ---- collect samples --------------------------------------------------------------------
$samples = @()
$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) 'borealis_bench_gates'
if ($Json -ne '') {
    if (-not (Test-Path $Json)) {
        Write-Host "FAIL  json sample not found: $Json"
        exit 1
    }
    $samples += (Get-Content $Json -Raw | ConvertFrom-Json)
    Write-Host "Samples: 1 (supplied JSON)"
} else {
    if ($Exe -eq '') {
        $Exe = Join-Path $BuildDir 'tools/borealis_bench.exe'
    }
    if (-not (Test-Path $Exe)) {
        Write-Host "FAIL  bench executable not found: $Exe"
        Write-Host "      build the optimized preset first (msvc-bench -> build-bench):"
        Write-Host '        tools/msvc_env.bat cmake --preset msvc-bench'
        Write-Host '        tools/msvc_env.bat cmake --build --preset msvc-bench --target borealis_bench'
        exit 1
    }
    if (-not (Test-Path $tmpDir)) {
        New-Item -ItemType Directory -Path $tmpDir | Out-Null
    }
    for ($i = 1; $i -le $Repeat; $i++) {
        $out = Join-Path $tmpDir "run$i.json"
        if (Test-Path $out) { Remove-Item $out -Force }
        & $Exe "--json=$out" | Out-Null
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $out)) {
            Write-Host "FAIL  bench run $i did not produce $out (exit $LASTEXITCODE)"
            exit 1
        }
        $samples += (Get-Content $out -Raw | ConvertFrom-Json)
    }
    Write-Host "Samples: $Repeat independent bench processes, median per metric"
}

$scenario = $samples[0].scenario
if ($scenario -ne $cfg.scenario) {
    Write-Host "FAIL  sample scenario '$scenario' does not match baseline '$($cfg.scenario)'"
    exit 1
}

# The optimization level is part of the measurement contract: a Debug reading of the same
# metric is roughly 30x off on the write side, so it must never be compared against an
# optimized reference (or vice versa). Both sides carry an explicit marker.
$wantConfig = $cfg.capture.build_config
if ($null -eq $wantConfig) {
    Write-Host "FAIL  baseline has no capture.build_config -- re-capture it (ruling 7.35)"
    exit 1
}
foreach ($s in $samples) {
    $got = $s.build_config
    if ($null -eq $got) {
        Write-Host "FAIL  a bench sample has no build_config -- rebuild borealis_bench from the current source"
        exit 1
    }
    if ($got -ne $wantConfig) {
        Write-Host "FAIL  bench build_config '$got' != baseline '$wantConfig' -- compare like with like"
        exit 1
    }
}
Write-Host "Build config: $wantConfig (matches baseline)"

# The grid the bench actually got belongs to the same contract, for the same reason: frame
# cost is per cell, so a reference captured at one grid cannot judge a run at another. The
# grid moves when the font subsystem's cell metrics move -- e.g. when the framework starts
# resolving the requested family instead of falling back through the default chain -- and
# that is precisely the pollution that otherwise surfaces as an unexplained +45%.
$wantGrid = $cfg.capture.grid
if ($null -eq $wantGrid) {
    Write-Host "FAIL  baseline has no capture.grid -- re-capture it"
    exit 1
}
$wantGridText = $wantGrid -join 'x'
foreach ($s in $samples) {
    if ($null -eq $s.grid) {
        Write-Host "FAIL  a bench sample has no grid -- rebuild borealis_bench from the current source"
        exit 1
    }
    $gotGridText = $s.grid -join 'x'
    if ($gotGridText -ne $wantGridText) {
        Write-Host "FAIL  bench grid $gotGridText != baseline $wantGridText -- cells per frame differ, compare like with like"
        exit 1
    }
}
Write-Host "Grid: $wantGridText (matches baseline)"

# ---- judge ------------------------------------------------------------------------------
$failures = 0
Write-Host ''
Write-Host ('{0,-5} {1,-22} {2,8} {3,10} {4,10} {5,24}  {6}' -f 'gate', 'metric', 'better', 'median', 'ref', 'limit', 'verdict')
foreach ($gate in $cfg.gates) {
    $metric = $gate.metric
    $value = $gate.reference
    $observed = @()
    foreach ($s in $samples) {
        $raw = $s.metrics.$metric
        if ($null -eq $raw) {
            Write-Host "FAIL  $metric missing from a bench sample -- the bench changed its metric names or the baseline drifted"
            $failures++
            $observed = @()
            break
        }
        $observed += [double]$raw
    }
    if ($observed.Count -eq 0) { continue }
    $median = Get-Median $observed

    $verdict = 'PASS'
    $limitText = 'n/a'
    if ($gate.direction -eq 'higher') {
        if ($gate.relative) {
            $relLimit = [double]$value * (1.0 - $regress / 100.0)
            $limitText = ('>= {0:N3}' -f $relLimit)
            if ($median -lt $relLimit) { $verdict = 'FAIL' }
        }
        if ($null -ne $gate.absolute_min) {
            $absLimit = [double]$gate.absolute_min
            if ($gate.relative) { $limitText += " / >= $absLimit abs" } else { $limitText = ('>= {0:N3} abs' -f $absLimit) }
            if ($median -lt $absLimit) { $verdict = 'FAIL' }
        }
    } else {
        if ($gate.relative) {
            $relLimit = [double]$value * (1.0 + $regress / 100.0)
            $limitText = ('<= {0:N3}' -f $relLimit)
            if ($median -gt $relLimit) { $verdict = 'FAIL' }
        }
        if ($null -ne $gate.absolute_max) {
            $absLimit = [double]$gate.absolute_max
            if ($gate.relative) { $limitText += " / <= $absLimit abs" } else { $limitText = ('<= {0:N3} abs' -f $absLimit) }
            if ($median -gt $absLimit) { $verdict = 'FAIL' }
        }
    }
    if ($verdict -eq 'FAIL') { $failures++ }

    Write-Host ('{0,-5} {1,-22} {2,8} {3,10:N3} {4,10:N3} {5,22}  {6}' -f `
        $gate.id, $metric, $gate.direction, $median, $value, $limitText, $verdict)
}

Write-Host ''
if ($failures -gt 0) {
    Write-Host "FAIL  $failures of $($cfg.gates.Count) perf gates violated"
    exit 1
}
Write-Host "PASS  all $($cfg.gates.Count) perf gates within baseline"
exit 0
