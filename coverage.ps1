#!/usr/bin/env pwsh

[CmdletBinding()]
param(
    [Parameter()]
    [string]$Configuration = "RelWithDebInfo",

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
    & cmake -S $ProjectRoot -B $ResolvedBuildDir
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed."
    }
}

Write-Info "Building test_basic ($Configuration)..."
$BuildArgs = @(
    "--build", $ResolvedBuildDir,
    "--config", $Configuration,
    "--target", "test_basic",
    "--parallel",
    "--",
    "/p:TrackFileAccess=false",
    "/nodeReuse:false"
)
& cmake @BuildArgs
if ($LASTEXITCODE -ne 0) {
    throw "Build failed."
}

if (Test-Path $ResolvedOutputDir) {
    Remove-Item $ResolvedOutputDir -Recurse -Force
}
New-Item -ItemType Directory -Path $ResolvedOutputDir | Out-Null

$TestExePath = Join-Path $ResolvedBuildDir "src\$Configuration\test_basic.exe"
if (-not (Test-Path $TestExePath)) {
    throw "Test executable not found: $TestExePath"
}

Write-Info "Collecting coverage from $TestExePath"
Push-Location (Join-Path $ResolvedBuildDir "src")
try {
    & $CoverageTool collect $TestExePath --output $CoverageXmlPath --output-format cobertura --nologo
    if ($LASTEXITCODE -ne 0) {
        throw "Coverage collection failed."
    }
}
finally {
    Pop-Location
}

$CoverageXml = [xml](Get-Content $CoverageXmlPath)
$PlcopenPackage = @($CoverageXml.coverage.packages.package) | Where-Object { $_.name -eq "plcopen" } | Select-Object -First 1
if (-not $PlcopenPackage) {
    throw "Coverage report did not contain the plcopen package."
}

$FileCoverage = foreach ($class in @($PlcopenPackage.classes.class)) {
    $covered = 0
    $valid = 0

    foreach ($line in @($class.lines.line)) {
        $valid += 1
        if ([int]$line.hits -gt 0) {
            $covered += 1
        }
    }

    [pscustomobject]@{
        File = Get-RelativeFilePath -ProjectRoot $ProjectRoot -FullPath $class.filename
        Covered = $covered
        Valid = $valid
    }
}

$AggregatedFiles = $FileCoverage |
    Group-Object File |
    ForEach-Object {
        $covered = ($_.Group | Measure-Object Covered -Sum).Sum
        $valid = ($_.Group | Measure-Object Valid -Sum).Sum

        [pscustomobject]@{
            file = $_.Name
            covered = [int]$covered
            valid = [int]$valid
            lineRate = if ($valid) { [math]::Round($covered / [double]$valid, 4) } else { 0.0 }
        }
    } |
    Where-Object { $_.valid -gt 0 } |
    Sort-Object lineRate, file

$LinesCovered = ($AggregatedFiles | Measure-Object covered -Sum).Sum
$LinesValid = ($AggregatedFiles | Measure-Object valid -Sum).Sum
$LineRate = if ($LinesValid) { $LinesCovered / [double]$LinesValid } else { 0.0 }
$TopWeakFiles = @($AggregatedFiles | Select-Object -First 10)

$Summary = [ordered]@{
    generatedAt = (Get-Date).ToString("s")
    configuration = $Configuration
    minimumLineRate = [math]::Round($MinimumLineRate, 4)
    module = "plcopen"
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
    "- Module: plcopen",
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

Write-Info ("plcopen line coverage: {0:P2} ({1}/{2})" -f $LineRate, $LinesCovered, $LinesValid)
Write-Info "Cobertura report: $CoverageXmlPath"
Write-Info "Summary: $SummaryMdPath"

if ($LineRate -lt $MinimumLineRate) {
    throw ("Coverage threshold not met: {0:P2} < {1:P2}" -f $LineRate, $MinimumLineRate)
}

Write-Success "Coverage threshold met."
