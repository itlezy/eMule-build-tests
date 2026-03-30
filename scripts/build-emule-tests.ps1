#Requires -Version 7.2
<#
.SYNOPSIS
Builds the shared standalone eMule unit-test executable for a workspace.

.PARAMETER WorkspaceRoot
The parent workspace root that contains the `eMule` and `tests` directories.

.PARAMETER Configuration
The Visual Studio configuration to build.

.PARAMETER Platform
The Visual Studio platform to build.

.PARAMETER Run
Runs the built test executable after a successful build.

.PARAMETER OutFile
Optional file that captures the test executable output when `-Run` is used.
#>
[CmdletBinding()]
param(
    [string]$WorkspaceRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidateSet('Win32', 'x64')]
    [string]$Platform = 'x64',

    [switch]$Run,

    [string]$OutFile
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

$workspaceRootPath = (Resolve-Path -LiteralPath $WorkspaceRoot).Path
$projectPath = Join-Path $workspaceRootPath 'tests\emule-tests.vcxproj'
$msbuildPath = Get-MSBuildPath

$arguments = @(
    $projectPath,
    '/m',
    '/nologo',
    '/t:Build',
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform"
)

Write-Output "Building $projectPath ($Platform|$Configuration)"
& $msbuildPath @arguments
if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with exit code $LASTEXITCODE."
}

if ($Run) {
    $binaryPath = Join-Path $workspaceRootPath ("tests\build\{0}\{1}\emule-tests.exe" -f $Platform, $Configuration)
    if (-not (Test-Path -LiteralPath $binaryPath)) {
        throw "Built test executable not found: $binaryPath"
    }

    Write-Output "Running $binaryPath"
    if ($OutFile) {
        $outDirectory = Split-Path -Parent $OutFile
        if ($outDirectory) {
            New-Item -ItemType Directory -Path $outDirectory -Force | Out-Null
        }
        & $binaryPath *>&1 | Tee-Object -FilePath $OutFile
    } else {
        & $binaryPath
    }
    if ($LASTEXITCODE -ne 0) {
        throw "The test executable failed with exit code $LASTEXITCODE."
    }
}
