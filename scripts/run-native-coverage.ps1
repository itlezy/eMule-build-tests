#Requires -Version 7.2
<#
.SYNOPSIS
Builds the shared native test binary and records OpenCppCoverage results for seam suites.

.DESCRIPTION
Runs the requested doctest suites from `emule-tests.exe` under OpenCppCoverage,
merges suite coverage into a single Cobertura report, and writes machine-readable
and human-readable summaries under `reports\native-coverage`.
#>
[CmdletBinding()]
param(
    [string]$TestRepoRoot = (Split-Path -Parent $PSScriptRoot),

    [string]$WorkspaceRoot = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'eMule-build-v0.72'),

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidateSet('Win32', 'x64')]
    [string]$Platform = 'x64',

    [string[]]$SuiteNames = @('parity', 'divergence'),

    [string]$PreferredCoverageRoot = 'C:\tools\ocppcov',

    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

function Get-BuildTag {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkspacePath
    )

    $leaf = Split-Path -Leaf $WorkspacePath
    if ([string]::IsNullOrWhiteSpace($leaf)) {
        throw "Unable to derive build tag from workspace path: $WorkspacePath"
    }

    return ($leaf -replace '[^A-Za-z0-9._-]', '_')
}

function Publish-DirectorySnapshot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourceDirectory,

        [Parameter(Mandatory = $true)]
        [string]$DestinationDirectory
    )

    if (Test-Path -LiteralPath $DestinationDirectory -PathType Container) {
        Remove-Item -LiteralPath $DestinationDirectory -Recurse -Force
    }

    New-Item -ItemType Directory -Force -Path $DestinationDirectory | Out-Null
    foreach ($entry in @(Get-ChildItem -LiteralPath $SourceDirectory -Force -ErrorAction SilentlyContinue)) {
        Copy-Item -LiteralPath $entry.FullName -Destination $DestinationDirectory -Recurse -Force
    }
}

function Get-ResolvedReportMetric {
    param(
        [Parameter(Mandatory = $true)]
        [xml]$CoverageXml,

        [Parameter(Mandatory = $true)]
        [string]$PropertyName
    )

    $value = $CoverageXml.coverage.$PropertyName
    if ($null -eq $value -or [string]::IsNullOrWhiteSpace([string]$value)) {
        return $null
    }

    return [string]$value
}

function Get-DoctestSuiteExecutionStats {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LogPath
    )

    $summaryLine = Get-Content -LiteralPath $LogPath | Where-Object { $_ -match '^\[doctest\] test cases:\s*(\d+)\s*\|\s*(\d+)\s*passed\s*\|\s*(\d+)\s*failed\s*\|\s*(\d+)\s*skipped' } | Select-Object -Last 1
    if (-not $summaryLine) {
        return $null
    }

    $match = [regex]::Match($summaryLine, '^\[doctest\] test cases:\s*(\d+)\s*\|\s*(\d+)\s*passed\s*\|\s*(\d+)\s*failed\s*\|\s*(\d+)\s*skipped')
    if (-not $match.Success) {
        return $null
    }

    $total = [int]$match.Groups[1].Value
    $passed = [int]$match.Groups[2].Value
    $failed = [int]$match.Groups[3].Value
    $skipped = [int]$match.Groups[4].Value
    $executed = $passed + $failed

    return [ordered]@{
        total = $total
        passed = $passed
        failed = $failed
        skipped = $skipped
        executed = $executed
    }
}

$testRepoRootPath = (Resolve-Path -LiteralPath $TestRepoRoot).Path
$workspaceRootPath = (Resolve-Path -LiteralPath $WorkspaceRoot).Path
$buildTag = Get-BuildTag -WorkspacePath $workspaceRootPath
$reportRoot = Join-Path $testRepoRootPath 'reports'
$coverageReportRoot = Join-Path $reportRoot 'native-coverage'
$reportStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runReportDir = Join-Path $coverageReportRoot ("{0}-{1}-{2}-{3}" -f $reportStamp, $buildTag, $Platform, $Configuration)
$latestReportDir = Join-Path $reportRoot 'native-coverage-latest'
$coverageSummaryPath = Join-Path $runReportDir 'coverage-summary.json'
$coverageSummaryTextPath = Join-Path $runReportDir 'coverage-summary.txt'
$coberturaPath = Join-Path $runReportDir 'coverage.cobertura.xml'

New-Item -ItemType Directory -Force -Path $runReportDir | Out-Null

if (-not $SkipBuild) {
    $buildScriptPath = Join-Path $testRepoRootPath 'scripts\build-emule-tests.ps1'
    & $buildScriptPath -TestRepoRoot $testRepoRootPath -WorkspaceRoot $workspaceRootPath -Configuration $Configuration -Platform $Platform -BuildTag $buildTag
    if ($LASTEXITCODE -ne 0) {
        throw "Shared test build failed with exit code $LASTEXITCODE."
    }
}

$binaryPath = Join-Path $testRepoRootPath ("build\{0}\{1}\{2}\emule-tests.exe" -f $buildTag, $Platform, $Configuration)
if (-not (Test-Path -LiteralPath $binaryPath -PathType Leaf)) {
    throw "Built test executable not found: $binaryPath"
}

$coverageBootstrapPath = Join-Path $testRepoRootPath 'helpers\helper-opencppcoverage-bootstrap.ps1'
$coverageExecutablePath = & $coverageBootstrapPath -TestRepoRoot $testRepoRootPath -PreferredInstallRoot $PreferredCoverageRoot
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($coverageExecutablePath)) {
    throw 'Unable to resolve OpenCppCoverage.'
}

$suiteRunSummaries = New-Object System.Collections.Generic.List[object]
$mergedBinaryCoveragePath = $null
$sourcePatterns = @(
    (Join-Path $testRepoRootPath 'src\*')
    (Join-Path $testRepoRootPath 'include\*')
    (Join-Path $workspaceRootPath 'eMule\*')
)
$excludedSourcePatterns = @(
    (Join-Path $testRepoRootPath 'third_party\*')
    (Join-Path $testRepoRootPath 'build\*')
    (Join-Path $testRepoRootPath 'reports\*')
    (Join-Path $workspaceRootPath 'eMule\srchybrid\x64\*')
    (Join-Path $workspaceRootPath 'eMule\res\*')
)

for ($index = 0; $index -lt $SuiteNames.Count; ++$index) {
    $suiteName = $SuiteNames[$index]
    $suiteLogPath = Join-Path $runReportDir ("suite-{0}.log" -f $suiteName)
    $suiteBinaryCoveragePath = Join-Path $runReportDir ("suite-{0}.cov" -f $suiteName)
    $coverageArguments = New-Object System.Collections.Generic.List[string]
    foreach ($modulePattern in @($binaryPath)) {
        $coverageArguments.Add('--modules')
        $coverageArguments.Add($modulePattern)
    }
    foreach ($sourcePattern in $sourcePatterns) {
        $coverageArguments.Add('--sources')
        $coverageArguments.Add($sourcePattern)
    }
    foreach ($excludedSourcePattern in $excludedSourcePatterns) {
        $coverageArguments.Add('--excluded_sources')
        $coverageArguments.Add($excludedSourcePattern)
    }
    if (-not [string]::IsNullOrWhiteSpace($mergedBinaryCoveragePath)) {
        $coverageArguments.Add('--input_coverage')
        $coverageArguments.Add($mergedBinaryCoveragePath)
    }
    $coverageArguments.Add('--working_dir')
    $coverageArguments.Add((Split-Path -Parent $binaryPath))
    $coverageArguments.Add('--export_type')
    $coverageArguments.Add(("binary:{0}" -f $suiteBinaryCoveragePath))
    if ($index -eq ($SuiteNames.Count - 1)) {
        $coverageArguments.Add('--export_type')
        $coverageArguments.Add(("cobertura:{0}" -f $coberturaPath))
    }
    $coverageArguments.Add('--')
    $coverageArguments.Add($binaryPath)
    $coverageArguments.Add('--no-intro')
    $coverageArguments.Add('--no-version')
    $coverageArguments.Add(("--test-suite={0}" -f $suiteName))

    & $coverageExecutablePath @coverageArguments *>&1 | Tee-Object -FilePath $suiteLogPath
    $suiteExitCode = $LASTEXITCODE
    $suiteStats = Get-DoctestSuiteExecutionStats -LogPath $suiteLogPath

    $suiteRunSummaries.Add([ordered]@{
        suite_name = $suiteName
        exit_code = $suiteExitCode
        log_path = $suiteLogPath
        binary_coverage_path = $suiteBinaryCoveragePath
        execution = $suiteStats
    })

    if ($suiteExitCode -ne 0) {
        throw "Coverage run for suite '$suiteName' failed with exit code $suiteExitCode."
    }

    if ($null -eq $suiteStats) {
        throw "Coverage run for suite '$suiteName' did not produce a doctest summary line."
    }

    if ($suiteStats.executed -le 0) {
        throw "Coverage run for suite '$suiteName' executed zero test cases."
    }

    $mergedBinaryCoveragePath = $suiteBinaryCoveragePath
}

if (-not (Test-Path -LiteralPath $coberturaPath -PathType Leaf)) {
    throw "Combined Cobertura coverage report was not generated: $coberturaPath"
}

[xml]$coverageXml = Get-Content -Raw -LiteralPath $coberturaPath
$linesCovered = [int](Get-ResolvedReportMetric -CoverageXml $coverageXml -PropertyName 'lines-covered')
$linesValid = [int](Get-ResolvedReportMetric -CoverageXml $coverageXml -PropertyName 'lines-valid')
$lineRateRaw = Get-ResolvedReportMetric -CoverageXml $coverageXml -PropertyName 'line-rate'
$lineRatePercent = if ($linesValid -gt 0) {
    [Math]::Round(($linesCovered / $linesValid) * 100.0, 2)
} elseif (-not [string]::IsNullOrWhiteSpace($lineRateRaw)) {
    [Math]::Round(([double]$lineRateRaw) * 100.0, 2)
} else {
    0.0
}

$summary = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    test_repo_root = $testRepoRootPath
    workspace_root = $workspaceRootPath
    configuration = $Configuration
    platform = $Platform
    build_tag = $buildTag
    binary_path = $binaryPath
    coverage_tool_path = $coverageExecutablePath
    report_dir = $runReportDir
    latest_report_dir = $latestReportDir
    cobertura_path = $coberturaPath
    suite_runs = @($suiteRunSummaries.ToArray())
    lines_covered = $linesCovered
    lines_valid = $linesValid
    line_rate_percent = $lineRatePercent
}

$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $coverageSummaryPath -Encoding utf8
@(
    'Native seam coverage'
    ("workspace_root: {0}" -f $workspaceRootPath)
    ("binary_path: {0}" -f $binaryPath)
    ("coverage_tool_path: {0}" -f $coverageExecutablePath)
    ("suite_names: {0}" -f ($SuiteNames -join ', '))
    ("lines_covered: {0}" -f $linesCovered)
    ("lines_valid: {0}" -f $linesValid)
    ("line_rate_percent: {0}" -f $lineRatePercent)
    ("cobertura_path: {0}" -f $coberturaPath)
    ("report_dir: {0}" -f $runReportDir)
) | Set-Content -LiteralPath $coverageSummaryTextPath -Encoding utf8

Publish-DirectorySnapshot -SourceDirectory $runReportDir -DestinationDirectory $latestReportDir

$publishHarnessSummaryPath = Join-Path $testRepoRootPath 'scripts\publish-harness-summary.ps1'
& $publishHarnessSummaryPath -TestRepoRoot $testRepoRootPath -CoverageSummaryPath $coverageSummaryPath
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to publish the combined harness summary after the native coverage run.'
}

Write-Output "Native coverage report directory: $runReportDir"
