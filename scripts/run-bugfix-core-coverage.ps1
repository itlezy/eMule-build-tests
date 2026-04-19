#Requires -Version 7.6
<#
.SYNOPSIS
Runs the canonical main-vs-bugfix native coverage and live-diff comparison slice.
#>
[CmdletBinding()]
param(
    [string]$TestRepoRoot = (Split-Path -Parent $PSScriptRoot),

    [string]$WorkspaceRoot,

    [string]$MainAppRoot,

    [string]$BugfixAppRoot,

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidateSet('x64')]
    [string]$Platform = 'x64',

    [string]$PreferredCoverageRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'resolve-workspace-layout.ps1')

function Get-LatestCoverageSummaryPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$TestRepoRootPath
    )

    $coverageRoot = Join-Path $TestRepoRootPath 'reports\native-coverage'
    $summary = Get-ChildItem -LiteralPath $coverageRoot -Filter 'coverage-summary.json' -Recurse -File |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if ($null -eq $summary) {
        throw "Unable to locate a native coverage summary under '$coverageRoot'."
    }

    return $summary.FullName
}

$testRepoRootPath = (Resolve-Path -LiteralPath $TestRepoRoot).Path
$workspaceRootPath = if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    Get-DefaultWorkspaceRootFromTestRepo -TestRepoRoot $testRepoRootPath
} else {
    (Resolve-Path -LiteralPath $WorkspaceRoot).Path
}

$mainAppRootPath = if ([string]::IsNullOrWhiteSpace($MainAppRoot)) {
    (Resolve-Path -LiteralPath (Join-Path $workspaceRootPath 'app\eMule-main')).Path
} else {
    (Resolve-Path -LiteralPath $MainAppRoot).Path
}

$bugfixAppRootPath = if ([string]::IsNullOrWhiteSpace($BugfixAppRoot)) {
    (Resolve-Path -LiteralPath (Join-Path $workspaceRootPath 'app\eMule-v0.72a-bugfix')).Path
} else {
    (Resolve-Path -LiteralPath $BugfixAppRoot).Path
}

$reportRoot = Join-Path $testRepoRootPath 'reports\bugfix-core-coverage'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runReportDir = Join-Path $reportRoot $stamp
New-Item -ItemType Directory -Force -Path $runReportDir | Out-Null

$nativeCoverageScriptPath = Join-Path $testRepoRootPath 'scripts\run-native-coverage.ps1'
$liveDiffScriptPath = Join-Path $testRepoRootPath 'scripts\run-live-diff.ps1'

& $nativeCoverageScriptPath `
    -TestRepoRoot $testRepoRootPath `
    -WorkspaceRoot $workspaceRootPath `
    -AppRoot $mainAppRootPath `
    -Configuration $Configuration `
    -Platform $Platform `
    -SuiteNames @('parity', 'bugfix-core-divergence') `
    -PreferredCoverageRoot $PreferredCoverageRoot

$mainCoverageSummaryPath = Get-LatestCoverageSummaryPath -TestRepoRootPath $testRepoRootPath

& $nativeCoverageScriptPath `
    -TestRepoRoot $testRepoRootPath `
    -WorkspaceRoot $workspaceRootPath `
    -AppRoot $bugfixAppRootPath `
    -Configuration $Configuration `
    -Platform $Platform `
    -SuiteNames @('parity') `
    -PreferredCoverageRoot $PreferredCoverageRoot

$bugfixCoverageSummaryPath = Get-LatestCoverageSummaryPath -TestRepoRootPath $testRepoRootPath

& $liveDiffScriptPath `
    -TestRepoRoot $testRepoRootPath `
    -DevWorkspaceRoot $workspaceRootPath `
    -DevAppRoot $mainAppRootPath `
    -OracleWorkspaceRoot $workspaceRootPath `
    -OracleAppRoot $bugfixAppRootPath `
    -Configuration $Configuration `
    -Platform $Platform `
    -SuiteNames @('parity', 'bugfix-core-divergence')

$liveDiffSummaryPath = Join-Path $testRepoRootPath 'reports\live-diff-summary.json'
$combinedSummaryPath = Join-Path $runReportDir 'bugfix-core-coverage-summary.json'

[ordered]@{
    generated_at = (Get-Date).ToString('o')
    workspace_root = $workspaceRootPath
    main_app_root = $mainAppRootPath
    bugfix_app_root = $bugfixAppRootPath
    configuration = $Configuration
    platform = $Platform
    main_coverage_summary = $mainCoverageSummaryPath
    bugfix_coverage_summary = $bugfixCoverageSummaryPath
    live_diff_summary = $liveDiffSummaryPath
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $combinedSummaryPath -Encoding utf8

Write-Output "Bugfix core coverage summary: $combinedSummaryPath"
