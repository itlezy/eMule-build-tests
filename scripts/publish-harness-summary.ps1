#Requires -Version 7.6
<#
.SYNOPSIS
Publishes a combined parity, coverage, and optional live-harness summary.
#>
[CmdletBinding()]
param(
    [string]$TestRepoRoot = (Split-Path -Parent $PSScriptRoot),

    [string]$CoverageSummaryPath = '',

    [string]$LiveDiffSummaryPath = '',

    [string]$LiveSessionManifestPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Read-JsonFile {
    param(
        [Parameter(Mandatory = $false)]
        [string]$Path
    )

    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }

    return Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json -Depth 12
}

$testRepoRootPath = (Resolve-Path -LiteralPath $TestRepoRoot).Path
$reportRoot = Join-Path $testRepoRootPath 'reports'
$defaultCoverageSummaryPath = Join-Path $reportRoot 'native-coverage-latest\coverage-summary.json'
$defaultLiveDiffSummaryPath = Join-Path $reportRoot 'live-diff-summary.json'
$resolvedCoverageSummaryPath = if ([string]::IsNullOrWhiteSpace($CoverageSummaryPath)) { $defaultCoverageSummaryPath } else { $CoverageSummaryPath }
$resolvedLiveDiffSummaryPath = if ([string]::IsNullOrWhiteSpace($LiveDiffSummaryPath)) { $defaultLiveDiffSummaryPath } else { $LiveDiffSummaryPath }

$coverageSummary = Read-JsonFile -Path $resolvedCoverageSummaryPath
$liveDiffSummary = Read-JsonFile -Path $resolvedLiveDiffSummaryPath
$liveSessionManifest = Read-JsonFile -Path $LiveSessionManifestPath

$combinedSummary = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    coverage = if ($null -ne $coverageSummary) {
        [ordered]@{
            report_dir = $coverageSummary.report_dir
            line_rate_percent = $coverageSummary.line_rate_percent
            lines_covered = $coverageSummary.lines_covered
            lines_valid = $coverageSummary.lines_valid
            suite_runs = $coverageSummary.suite_runs
        }
    } else {
        $null
    }
    parity = if ($null -ne $liveDiffSummary) {
        [ordered]@{
            report_root = $liveDiffSummary.report_root
            failed = [bool]$liveDiffSummary.failed
            suites = $liveDiffSummary.suites
        }
    } else {
        $null
    }
    live_harness = if ($null -ne $liveSessionManifest) {
        [ordered]@{
            launch_status = $liveSessionManifest.launch_status
            scenario_profile = $liveSessionManifest.scenario_profile
            cleanup_success = $liveSessionManifest.cleanup_success
            leftover_process_ids = $liveSessionManifest.leftover_process_ids
            artifact_dir = $liveSessionManifest.artifact_dir
            profile_root = $liveSessionManifest.profile_root
        }
    } else {
        $null
    }
}

$jsonPath = Join-Path $reportRoot 'harness-summary.json'
$textPath = Join-Path $reportRoot 'harness-summary.txt'
$combinedSummary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath -Encoding utf8
@(
    'Harness summary'
    ("coverage_available: {0}" -f ($null -ne $coverageSummary))
    ("parity_available: {0}" -f ($null -ne $liveDiffSummary))
    ("live_harness_available: {0}" -f ($null -ne $liveSessionManifest))
    ("coverage_line_rate_percent: {0}" -f $(if ($null -ne $coverageSummary) { $coverageSummary.line_rate_percent } else { '' }))
    ("parity_failed: {0}" -f $(if ($null -ne $liveDiffSummary) { [bool]$liveDiffSummary.failed } else { '' }))
    ("live_cleanup_success: {0}" -f $(if ($null -ne $liveSessionManifest) { $liveSessionManifest.cleanup_success } else { '' }))
) | Set-Content -LiteralPath $textPath -Encoding utf8

Write-Output "Harness summary JSON: $jsonPath"
