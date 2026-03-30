#Requires -Version 7.2
<#
.SYNOPSIS
Builds and runs the shared eMule test binary in two workspaces and compares the text output.

.PARAMETER DevWorkspaceRoot
The development workspace root.

.PARAMETER OracleWorkspaceRoot
The oracle workspace root.

.PARAMETER Configuration
The Visual Studio configuration to build.

.PARAMETER Platform
The Visual Studio platform to build.
#>
[CmdletBinding()]
param(
    [string]$DevWorkspaceRoot = 'C:\prj\p2p\eMulebb',
    [string]$OracleWorkspaceRoot = 'C:\prj\p2p\eMulebb-oracle',

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidateSet('Win32', 'x64')]
    [string]$Platform = 'x64'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-TestRun {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkspaceRoot,

        [Parameter(Mandatory = $true)]
        [string]$OutputPath
    )

    $scriptPath = Join-Path $WorkspaceRoot 'tests\scripts\build-emule-tests.ps1'
    if (-not (Test-Path -LiteralPath $scriptPath)) {
        throw "Shared test build script not found: $scriptPath"
    }

    & $scriptPath -WorkspaceRoot $WorkspaceRoot -Configuration $Configuration -Platform $Platform -Run -OutFile $OutputPath
}

$reportRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'reports'
New-Item -ItemType Directory -Path $reportRoot -Force | Out-Null

$devOutput = Join-Path $reportRoot 'dev-output.txt'
$oracleOutput = Join-Path $reportRoot 'oracle-output.txt'

Invoke-TestRun -WorkspaceRoot $DevWorkspaceRoot -OutputPath $devOutput
Invoke-TestRun -WorkspaceRoot $OracleWorkspaceRoot -OutputPath $oracleOutput

$devText = Get-Content -Raw -LiteralPath $devOutput
$oracleText = Get-Content -Raw -LiteralPath $oracleOutput

if ($devText -eq $oracleText) {
    Write-Output 'Live test outputs match between dev and oracle workspaces.'
    return
}

Write-Warning 'Live test outputs differ between dev and oracle workspaces.'
Write-Output "Dev output: $devOutput"
Write-Output "Oracle output: $oracleOutput"
