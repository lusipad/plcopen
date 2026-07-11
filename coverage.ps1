#!/usr/bin/env pwsh

[CmdletBinding()]
param(
    # Coverage must run on unoptimized code: with /O1+/O2 the header-inline
    # methods get folded into their callers and the tool attributes them 0 hits.
    [Parameter()]
    [string]$Configuration = "Debug",

    [Parameter()]
    [double]$MinimumLineRate = 0.50,

    [Parameter()]
    [string]$BuildDir = "build",

    [Parameter()]
    [string]$OutputDir = "out/coverage"
)

$ErrorActionPreference = "Stop"

function Write-Info {
    param([string]$Message)
    Write-Host "ℹ️  $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "✅ $Message" -ForegroundColor Green
}

function Get-CodeCoverageTool {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $installationPath = & $vswhere -latest -products * -property installationPath 2>$null
        if ($LASTEXITCODE -eq 0 -and $installationPath) {
            $candidate = Join-Path $installationPath "Common7\IDE\Extensions\Microsoft\CodeCoverage.Console\Microsoft.CodeCoverage.Console.exe"
            if (Test-Path $candidate) {
                return $candidate
            }
        }
    }

    $fallback = Get-ChildItem "C:\Program Files\Microsoft Visual Studio" -Recurse -Filter Microsoft.CodeCoverage.Console.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName

    if ($fallback) {
        return $fallback
    }

    throw "Microsoft.CodeCoverage.Console.exe not found. Install Visual Studio code coverage tooling first."
}

function Get-RelativeFilePath {
    param(
        [string]$ProjectRoot,
        [string]$FullPath
    )

    if ($FullPath.StartsWith($ProjectRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $FullPath.Substring($ProjectRoot.Length).TrimStart('\')
    }

    return $FullPath
}

function Get-CMakeGenerator {
    param([string]$ResolvedBuildDir)

    $CachePath = Join-Path $ResolvedBuildDir "CMakeCache.txt"
    if (-not (Test-Path $CachePath)) {
        return ""
    }

    $GeneratorLine = Select-String -Path $CachePath -Pattern "^CMAKE_GENERATOR:INTERNAL=" | Select-Object -First 1
    if (-not $GeneratorLine) {
        return ""
    }

    return ($GeneratorLine.Line -replace "^CMAKE_GENERATOR:INTERNAL=", "")
}

# Coverage runs every deterministic core executable (family test suites,
# oracle/fuzz, replay regression, demos); a single suite badly understates
# the surface now that acceptance tests are split per family.
# Wall-clock-gated benchmarks are excluded: under dynamic coverage
# instrumentation a latency gate measures the instrumentation, not the
# budget (2026-07-11 CI instance: cartesian_ik 158us vs 50us gate). Those
# gates stay enforced by the uninstrumented ctest step.
$ExcludedFromCoverage = @(
    "plcopen_core_benchmark"
)

function Get-CoverageExecutables {
    param(
        [string]$ResolvedBuildDir,
        [string]$Configuration
    )

    $SearchDirs = @(
        (Join-Path $ResolvedBuildDir "core\$Configuration"),
        (Join-Path $ResolvedBuildDir "core")
    )

    foreach ($Dir in $SearchDirs) {
        if (-not (Test-Path $Dir)) {
            continue
        }
        $Found = Get-ChildItem -Path $Dir -File -Filter 'plcopen_core_*.exe' -ErrorAction SilentlyContinue |
            Where-Object { $ExcludedFromCoverage -notcontains $_.BaseName }
        if ($Found) {
            return @($Found | Sort-Object Name | Select-Object -ExpandProperty FullName)
        }
    }

    return @()
}

$ProjectRoot = (Resolve-Path $PSScriptRoot).Path
$ResolvedBuildDir = Join-Path $ProjectRoot $BuildDir
$ResolvedOutputDir = Join-Path $ProjectRoot $OutputDir
$CoverageXmlPath = Join-Path $ResolvedOutputDir "coverage.cobertura.xml"
$SummaryJsonPath = Join-Path $ResolvedOutputDir "coverage-summary.json"
$SummaryMdPath = Join-Path $ResolvedOutputDir "coverage-summary.md"

$CoverageTool = Get-CodeCoverageTool
Write-Info "Using code coverage tool: $CoverageTool"

if (-not (Test-Path $ResolvedBuildDir)) {
    Write-Info "Configuring CMake project..."
    & cmake -S $ProjectRoot -B $ResolvedBuildDir -DCMAKE_BUILD_TYPE=$Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed."
    }
}

Write-Info "Building core targets ($Configuration)..."
$Generator = Get-CMakeGenerator -ResolvedBuildDir $ResolvedBuildDir
$BuildArgs = @(
    "--build", $ResolvedBuildDir,
    "--config", $Configuration
)
if ($Generator -notlike "NMake*") {
    $BuildArgs += "--parallel"
}
if ($Generator -like "Visual Studio*") {
    $BuildArgs += @(
        "--",
        "/p:TrackFileAccess=false",
        "/nodeReuse:false"
    )
}
& cmake @BuildArgs
if ($LASTEXITCODE -ne 0) {
    throw "Build failed."
}

if (Test-Path $ResolvedOutputDir) {
    Remove-Item $ResolvedOutputDir -Recurse -Force
}
New-Item -ItemType Directory -Path $ResolvedOutputDir | Out-Null

$TestExecutables = Get-CoverageExecutables -ResolvedBuildDir $ResolvedBuildDir -Configuration $Configuration
if (-not $TestExecutables -or $TestExecutables.Count -eq 0) {
    throw "No plcopen_core_* executables found under $ResolvedBuildDir"
}

# Per-executable arguments for tools that need them.
$ExecutableArgs = @{
    "plcopen_core_replay_regression" = @((Join-Path $ProjectRoot "testdata\replay"))
}

$RawCoverageFiles = @()
Push-Location $ResolvedBuildDir
try {
    foreach ($TestExePath in $TestExecutables) {
        $ExeName = [System.IO.Path]::GetFileNameWithoutExtension($TestExePath)
        $RawCoveragePath = Join-Path $ResolvedOutputDir "$ExeName.coverage"
        $ExtraArgs = @()
        if ($ExecutableArgs.ContainsKey($ExeName)) {
            $ExtraArgs = $ExecutableArgs[$ExeName]
        }
        Write-Info "Collecting coverage from $ExeName"
        & $CoverageTool collect $TestExePath @ExtraArgs --output $RawCoveragePath --nologo
        if ($LASTEXITCODE -ne 0) {
            throw "Coverage collection failed for $ExeName."
        }
        $RawCoverageFiles += $RawCoveragePath
    }

    Write-Info "Merging $($RawCoverageFiles.Count) coverage sessions"
    & $CoverageTool merge @RawCoverageFiles --output $CoverageXmlPath --output-format cobertura --nologo
    if ($LASTEXITCODE -ne 0) {
        throw "Coverage merge failed."
    }
}
finally {
    Pop-Location
}

$CoverageXml = [xml](Get-Content $CoverageXmlPath)
$CoverageClasses = foreach ($Package in @($CoverageXml.coverage.packages.package)) {
    @($Package.classes.class)
}

# The merged report repeats a file once per coverage session, so line hits must
# be unioned per line number instead of summed per class entry.
$FileLineHits = @{}
foreach ($class in $CoverageClasses) {
    $RelativeFile = Get-RelativeFilePath -ProjectRoot $ProjectRoot -FullPath $class.filename
    if ($RelativeFile -notlike "core\*" -and $RelativeFile -notlike "core/*") {
        continue
    }

    if (-not $FileLineHits.ContainsKey($RelativeFile)) {
        $FileLineHits[$RelativeFile] = @{}
    }
    $LineHits = $FileLineHits[$RelativeFile]
    foreach ($line in @($class.lines.line)) {
        $LineNumber = [int]$line.number
        $Hit = [int]$line.hits -gt 0
        if ($LineHits.ContainsKey($LineNumber)) {
            $LineHits[$LineNumber] = $LineHits[$LineNumber] -or $Hit
        }
        else {
            $LineHits[$LineNumber] = $Hit
        }
    }
}

if ($FileLineHits.Count -eq 0) {
    throw "Coverage report did not contain core files."
}

$AggregatedFiles = foreach ($File in $FileLineHits.Keys) {
    $LineHits = $FileLineHits[$File]
    $valid = $LineHits.Count
    $covered = @($LineHits.Values | Where-Object { $_ }).Count

    [pscustomobject]@{
        file = $File
        covered = [int]$covered
        valid = [int]$valid
        lineRate = if ($valid) { [math]::Round($covered / [double]$valid, 4) } else { 0.0 }
    }
}
$AggregatedFiles = @($AggregatedFiles | Where-Object { $_.valid -gt 0 } | Sort-Object lineRate, file)

$LinesCovered = ($AggregatedFiles | Measure-Object covered -Sum).Sum
$LinesValid = ($AggregatedFiles | Measure-Object valid -Sum).Sum
$LineRate = if ($LinesValid) { $LinesCovered / [double]$LinesValid } else { 0.0 }
$TopWeakFiles = @($AggregatedFiles | Select-Object -First 10)

$Summary = [ordered]@{
    generatedAt = (Get-Date).ToString("s")
    configuration = $Configuration
    minimumLineRate = [math]::Round($MinimumLineRate, 4)
    module = "plcopen_core"
    lineRate = [math]::Round($LineRate, 4)
    linesCovered = [int]$LinesCovered
    linesValid = [int]$LinesValid
    passed = ($LineRate -ge $MinimumLineRate)
    report = @{
        cobertura = $CoverageXmlPath
    }
    weakestFiles = $TopWeakFiles
}

$Summary | ConvertTo-Json -Depth 5 | Set-Content $SummaryJsonPath

$SummaryLines = @(
    "# Coverage Summary",
    "",
    "- Generated: $($Summary.generatedAt)",
    "- Configuration: $Configuration",
    "- Module: plcopen_core",
    ("- Line coverage: {0:P2} ({1}/{2})" -f $LineRate, $LinesCovered, $LinesValid),
    ("- Threshold: {0:P2}" -f $MinimumLineRate),
    ("- Result: {0}" -f ($(if ($Summary.passed) { "PASS" } else { "FAIL" }))),
    ("- Cobertura report: $CoverageXmlPath"),
    "",
    "## Weakest Files",
    ""
)

foreach ($file in $TopWeakFiles) {
    $SummaryLines += "- {0}: {1:P2} ({2}/{3})" -f $file.file, $file.lineRate, $file.covered, $file.valid
}

$SummaryLines | Set-Content $SummaryMdPath

Write-Info ("plcopen_core line coverage: {0:P2} ({1}/{2})" -f $LineRate, $LinesCovered, $LinesValid)
Write-Info "Cobertura report: $CoverageXmlPath"
Write-Info "Summary: $SummaryMdPath"

if ($LineRate -lt $MinimumLineRate) {
    throw ("Coverage threshold not met: {0:P2} < {1:P2}" -f $LineRate, $MinimumLineRate)
}

Write-Success "Coverage threshold met."
