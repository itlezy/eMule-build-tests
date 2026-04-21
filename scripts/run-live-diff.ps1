#Requires -Version 7.6
<#
.SYNOPSIS
Obsolete compatibility shim for the Python live-diff runner.

.DESCRIPTION
This entrypoint is retained for workspace.ps1 and existing operator commands.
The historical PowerShell implementation was moved to
`scripts\obsolete\run-live-diff.ps1`; new work belongs in
`scripts\run_live_diff.py`.
#>
[CmdletBinding()]
param(
    [string]$TestRepoRoot = (Split-Path -Parent $PSScriptRoot),

    [string]$DevWorkspaceRoot,
    [string]$OracleWorkspaceRoot,
    [string]$DevAppRoot,
    [string]$OracleAppRoot,

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidateSet('x64')]
    [string]$Platform = 'x64',

    [string[]]$SuiteNames = @('parity', 'divergence')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

function Get-PythonInvocation {
    [CmdletBinding()]
    param()

    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
        return @{
            FilePath = $python.Source
            Prefix = @()
        }
    }

    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        return @{
            FilePath = $py.Source
            Prefix = @('-3')
        }
    }

    throw 'Python 3 was not found on PATH.'
}

$testRepoRootPath = (Resolve-Path -LiteralPath $TestRepoRoot).Path
$runnerPath = Join-Path $testRepoRootPath 'scripts\run_live_diff.py'
if (-not (Test-Path -LiteralPath $runnerPath -PathType Leaf)) {
    throw "Python live-diff runner not found: $runnerPath"
}

$arguments = @(
    $runnerPath,
    '--test-repo-root', $testRepoRootPath,
    '--configuration', $Configuration,
    '--platform', $Platform
)

if (-not [string]::IsNullOrWhiteSpace($DevWorkspaceRoot)) {
    $arguments += @('--dev-workspace-root', $DevWorkspaceRoot)
}
if (-not [string]::IsNullOrWhiteSpace($OracleWorkspaceRoot)) {
    $arguments += @('--oracle-workspace-root', $OracleWorkspaceRoot)
}
if (-not [string]::IsNullOrWhiteSpace($DevAppRoot)) {
    $arguments += @('--dev-app-root', $DevAppRoot)
}
if (-not [string]::IsNullOrWhiteSpace($OracleAppRoot)) {
    $arguments += @('--oracle-app-root', $OracleAppRoot)
}
foreach ($suiteName in $SuiteNames) {
    $arguments += @('--suite-name', $suiteName)
}

Write-Warning 'scripts\run-live-diff.ps1 is obsolete; use scripts\run_live_diff.py for new work.'
$pythonInvocation = Get-PythonInvocation
& $pythonInvocation.FilePath @($pythonInvocation.Prefix + $arguments)
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
