#Requires -Version 7.2
<#
.SYNOPSIS
Resolves a usable OpenCppCoverage installation for shared native coverage runs.

.DESCRIPTION
Prefers the operator-provided installation under `C:\tools\ocppcov`, and falls back
to a repo-managed pinned install under `tools\OpenCppCoverage\<version>` when the
preferred path is unavailable.
#>
[CmdletBinding()]
param(
    [string]$TestRepoRoot = (Split-Path -Parent $PSScriptRoot),

    [string]$PreferredInstallRoot = 'C:\tools\ocppcov',

    [string]$Version = '0.9.9.0'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-OpenCppCoverageExecutablePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InstallRoot
    )

    $candidatePath = Join-Path $InstallRoot 'OpenCppCoverage.exe'
    if (Test-Path -LiteralPath $candidatePath -PathType Leaf) {
        return (Resolve-Path -LiteralPath $candidatePath).Path
    }

    return $null
}

$preferredExecutablePath = Get-OpenCppCoverageExecutablePath -InstallRoot $PreferredInstallRoot
if ($preferredExecutablePath) {
    Write-Output $preferredExecutablePath
    exit 0
}

$testRepoRootPath = (Resolve-Path -LiteralPath $TestRepoRoot).Path
$toolsRoot = Join-Path $testRepoRootPath 'tools\OpenCppCoverage'
$downloadRoot = Join-Path $toolsRoot 'downloads'
$installRoot = Join-Path $toolsRoot $Version
$installerPath = Join-Path $downloadRoot ("OpenCppCoverageSetup-x64-{0}.exe" -f $Version)
$downloadUrl = "https://github.com/OpenCppCoverage/OpenCppCoverage/releases/download/release-{0}/OpenCppCoverageSetup-x64-{0}.exe" -f $Version
$repoManagedExecutablePath = Get-OpenCppCoverageExecutablePath -InstallRoot $installRoot

if ($repoManagedExecutablePath) {
    Write-Output $repoManagedExecutablePath
    exit 0
}

New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
New-Item -ItemType Directory -Force -Path $installRoot | Out-Null

if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
    Invoke-WebRequest -Uri $downloadUrl -OutFile $installerPath
}

$installerArguments = @(
    '/SP-'
    '/VERYSILENT'
    '/SUPPRESSMSGBOXES'
    '/NORESTART'
    ("/DIR={0}" -f $installRoot)
)

<#
* @brief Keep coverage tooling repo-local when the preferred shared install is missing.
#>
$installerProcess = Start-Process -FilePath $installerPath -ArgumentList $installerArguments -Wait -PassThru -WindowStyle Hidden
if ($installerProcess.ExitCode -ne 0) {
    throw "OpenCppCoverage installer failed with exit code $($installerProcess.ExitCode)."
}

$repoManagedExecutablePath = Get-OpenCppCoverageExecutablePath -InstallRoot $installRoot
if (-not $repoManagedExecutablePath) {
    throw "OpenCppCoverage.exe was not installed into '$installRoot'."
}

Write-Output $repoManagedExecutablePath
