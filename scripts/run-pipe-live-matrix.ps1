<#
.SYNOPSIS
Runs the recommended Pipe API live matrix and soak profile.

.DESCRIPTION
This wrapper keeps the preferred live coverage invocation under the shared
tests workspace while delegating execution to the app-side helper that launches
eMule, the remote sidecar, and the named-pipe-backed API scenarios.

.PARAMETER ProfileRoot
Base directory that stores per-run live harness working profiles.

.PARAMETER SeedRoot
Optional deterministic seed root that is copied into each fresh working profile.

.PARAMETER SessionManifestPath
Optional machine-readable manifest output path for launch-only or full-run automation.

.PARAMETER LaunchOnly
Starts the live harness session and exits after the sidecar is healthy without running the matrix or soak workload.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$ProfileRoot = '',

    [Parameter(Mandatory = $false)]
    [string]$SeedRoot = '',

    [Parameter(Mandatory = $false)]
    [string]$SessionManifestPath = '',

    [Parameter(Mandatory = $false)]
    [string]$SearchQuery = '1080p',

    [Parameter(Mandatory = $false)]
    [string[]]$StressQueries = @('1080p x265', '1080p bluray'),

    [Parameter(Mandatory = $false)]
    [ValidateSet('balanced', 'matrix', 'soak')]
    [string]$ScenarioProfile = 'balanced',

    [Parameter(Mandatory = $false)]
    [int]$MatrixRepeatCount = 1,

    [switch]$LaunchOnly,
    [switch]$StrictMatrix,
    [switch]$SkipBuild,
    [switch]$KeepRunning
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProfileRoot)) {
    $ProfileRoot = Join-Path (Join-Path (Split-Path -Parent $PSScriptRoot) 'reports') 'live-profiles'
}

$helperPath = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\eMule-build\eMule\helpers\helper-runtime-pipe-live-session.ps1'))
if (-not (Test-Path -LiteralPath $helperPath -PathType Leaf)) {
    throw "Pipe live helper '$helperPath' was not found."
}

& $helperPath `
    -ProfileRoot $ProfileRoot `
    -SeedRoot $SeedRoot `
    -SessionManifestPath $SessionManifestPath `
    -SearchQuery $SearchQuery `
    -StressQueries $StressQueries `
    -ScenarioProfile $ScenarioProfile `
    -MatrixRepeatCount $MatrixRepeatCount `
    -LaunchOnly:$LaunchOnly `
    -StrictMatrix:$StrictMatrix `
    -SearchWaitSec 30 `
    -SearchCycleCount 2 `
    -SearchCyclePauseSec 5 `
    -MonitorSec 180 `
    -PollSec 5 `
    -TransferProbeCount 2 `
    -UploadProbeCount 1 `
    -ExtraStatsBurstsPerPoll 1 `
    -TransferChurnCycles 6 `
    -TransfersPerChurnCycle 3 `
    -TransferChurnPauseMs 500 `
    -PipeWarmupSec 12 `
    -SkipBuild:$SkipBuild `
    -KeepRunning:$KeepRunning
