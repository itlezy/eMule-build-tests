#Requires -Version 7.2
<#
.SYNOPSIS
Builds the shared standalone eMule unit-test executable for a workspace.

.PARAMETER TestRepoRoot
The root of the shared `eMule-build-tests` repository.

.PARAMETER WorkspaceRoot
The workspace root that contains the target `eMule` checkout.

.PARAMETER Configuration
The Visual Studio configuration to build.

.PARAMETER Platform
The Visual Studio platform to build.

.PARAMETER Run
Runs the built test executable after a successful build.

.PARAMETER OutFile
Optional file that captures the test executable output when `-Run` is used.

.PARAMETER AllowTestFailure
Leaves the PowerShell command successful even if the test executable returns a failing exit code.

.PARAMETER SkipTrackedFilePrivacyGuard
Skips the tracked-file privacy guard that validates the repo's committed assets.

.PARAMETER TestArguments
Additional arguments passed to the test executable when `-Run` is used.
#>
[CmdletBinding()]
param(
    [string]$TestRepoRoot = (Split-Path -Parent $PSScriptRoot),

    [string]$WorkspaceRoot = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'eMule-build'),

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidateSet('Win32', 'x64')]
    [string]$Platform = 'x64',

    [switch]$Run,

    [string]$OutFile,

    [switch]$AllowTestFailure,

    [string]$BuildTag,

    [switch]$SkipTrackedFilePrivacyGuard,

    [string[]]$TestArguments = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

function Resolve-FirstExisting {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Paths
    )

    foreach ($path in $Paths) {
        if ($path -and (Test-Path -LiteralPath $path)) {
            return (Resolve-Path -LiteralPath $path).Path
        }
    }

    return $null
}

function Get-VsWherePath {
    $cmd = Get-Command 'vswhere.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd) {
        return $cmd.Source
    }

    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)}) | Where-Object { $_ }) {
        $candidate = Join-Path $base 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    return $null
}

function Get-MSBuildPath {
    $cmd = Get-Command 'MSBuild.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd) {
        return $cmd.Source
    }

    $vsWhere = Get-VsWherePath
    if ($vsWhere) {
        $installationPath = & $vsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($LASTEXITCODE -eq 0 -and $installationPath) {
            $candidate = Join-Path $installationPath 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    foreach ($base in @($env:ProgramFiles, ${env:ProgramFiles(x86)}) | Where-Object { $_ }) {
        $candidate = Resolve-FirstExisting @(
            (Join-Path $base 'Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe'),
            (Join-Path $base 'Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe'),
            (Join-Path $base 'Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'),
            (Join-Path $base 'Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe')
        )
        if ($candidate) {
            return $candidate
        }
    }

    throw 'MSBuild.exe not found.'
}

function Get-BuildTag {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkspacePath
    )

    $leaf = Split-Path -Leaf $WorkspacePath
    if ([string]::IsNullOrWhiteSpace($leaf)) {
        throw "Unable to derive build tag from workspace path: $WorkspacePath"
    }

    ($leaf -replace '[^A-Za-z0-9._-]', '_')
}

$testRepoRootPath = (Resolve-Path -LiteralPath $TestRepoRoot).Path
$workspaceRootPath = (Resolve-Path -LiteralPath $WorkspaceRoot).Path
if ([string]::IsNullOrWhiteSpace($BuildTag)) {
    $BuildTag = Get-BuildTag -WorkspacePath $workspaceRootPath
}

if (-not $SkipTrackedFilePrivacyGuard) {
    $trackedFilePrivacyGuardPath = Join-Path $testRepoRootPath 'scripts\guard-tracked-files.ps1'
    if (-not (Test-Path -LiteralPath $trackedFilePrivacyGuardPath -PathType Leaf)) {
        throw "Tracked-file privacy guard not found at '$trackedFilePrivacyGuardPath'."
    }

    <#
    * @brief Fail fast on committed local-path or personal-identifier leaks before any build work starts.
    #>
    try {
        & $trackedFilePrivacyGuardPath -RepoRoot $testRepoRootPath
    } catch {
        throw 'Tracked-file privacy guard failed.'
    }
}

$projectPath = Join-Path $testRepoRootPath 'emule-tests.vcxproj'
$msbuildPath = Get-MSBuildPath

$arguments = @(
    $projectPath,
    '/m',
    '/nologo',
    '/t:Build',
    "/p:WorkspaceRoot=$workspaceRootPath",
    "/p:BuildTag=$BuildTag",
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform"
)

Write-Output "Building $projectPath for $workspaceRootPath ($Platform|$Configuration, tag=$BuildTag)"
& $msbuildPath @arguments
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with exit code $LASTEXITCODE."
}

if ($Run) {
    $binaryPath = Join-Path $testRepoRootPath ("build\{0}\{1}\{2}\emule-tests.exe" -f $BuildTag, $Platform, $Configuration)
    if (-not (Test-Path -LiteralPath $binaryPath)) {
        throw "Built test executable not found: $binaryPath"
    }

    Write-Output "Running $binaryPath"
    if ($OutFile) {
        $outDirectory = Split-Path -Parent $OutFile
        if ($outDirectory) {
            New-Item -ItemType Directory -Path $outDirectory -Force | Out-Null
        }
        & $binaryPath @TestArguments *>&1 | Tee-Object -FilePath $OutFile
    } else {
        & $binaryPath @TestArguments
    }
    if ($LASTEXITCODE -ne 0 -and -not $AllowTestFailure) {
        throw "The test executable failed with exit code $LASTEXITCODE."
    }

    if ($LASTEXITCODE -ne 0 -and $AllowTestFailure) {
        Write-Warning "The test executable failed with exit code $LASTEXITCODE."
    }
}
