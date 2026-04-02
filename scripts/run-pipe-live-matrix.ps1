<#
.SYNOPSIS
Runs the recommended Pipe API live matrix and soak profile.

.DESCRIPTION
This wrapper keeps the preferred live coverage invocation under the shared
tests workspace while delegating execution to the app-side helper that launches
eMule, the remote sidecar, and the named-pipe-backed API scenarios.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$ProfileRoot = 'C:\tmp\emule-testing',

    [Parameter(Mandatory = $false)]
    [string]$SearchQuery = '1080p',

    [Parameter(Mandatory = $false)]
    [string[]]$StressQueries = @('1080p x265', '1080p bluray'),

    [Parameter(Mandatory = $false)]
    [ValidateSet('balanced', 'matrix', 'soak')]
    [string]$ScenarioProfile = 'balanced',

    [Parameter(Mandatory = $false)]
    [int]$MatrixRepeatCount = 1,

    [switch]$StrictMatrix,
    [switch]$SkipBuild,
    [switch]$KeepRunning
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$helperPath = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\eMule-build\eMule\helpers\helper-runtime-pipe-live-session.ps1'))
if (-not (Test-Path -LiteralPath $helperPath -PathType Leaf)) {
    throw "Pipe live helper '$helperPath' was not found."
}

& $helperPath `
    -ProfileRoot $ProfileRoot `
    -SearchQuery $SearchQuery `
    -StressQueries $StressQueries `
    -ScenarioProfile $ScenarioProfile `
    -MatrixRepeatCount $MatrixRepeatCount `
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
