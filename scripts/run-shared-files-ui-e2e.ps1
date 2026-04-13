#Requires -Version 7.6

param(
    [string]$WorkspaceRoot,
    [string]$AppRoot,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$ArtifactsRoot,
    [switch]$KeepArtifacts
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'resolve-workspace-layout.ps1')
. (Join-Path $PSScriptRoot 'resolve-app-root.ps1')

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

function Invoke-PythonChecked {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [hashtable]$PythonInvocation,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,

        [hashtable]$Environment = @{}
    )

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $PythonInvocation.FilePath
    foreach ($argument in @($PythonInvocation.Prefix + $Arguments)) {
        [void]$psi.ArgumentList.Add($argument)
    }
    $psi.UseShellExecute = $false
    foreach ($entry in $Environment.GetEnumerator()) {
        $psi.Environment[$entry.Key] = [string]$entry.Value
    }

    $process = [System.Diagnostics.Process]::Start($psi)
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "Python command failed with exit code $($process.ExitCode): $($psi.FileName) $([string]::Join(' ', $psi.ArgumentList))"
    }
}

function Test-PythonImport {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [hashtable]$PythonInvocation,

        [Parameter(Mandatory = $true)]
        [string]$ModuleName,

        [hashtable]$Environment = @{}
    )

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $PythonInvocation.FilePath
    foreach ($argument in @($PythonInvocation.Prefix + @('-c', "import $ModuleName"))) {
        [void]$psi.ArgumentList.Add($argument)
    }
    $psi.UseShellExecute = $false
    foreach ($entry in $Environment.GetEnumerator()) {
        $psi.Environment[$entry.Key] = [string]$entry.Value
    }

    $process = [System.Diagnostics.Process]::Start($psi)
    $process.WaitForExit()
    return ($process.ExitCode -eq 0)
}

function Get-AppExecutablePath {
    [CmdletBinding()]
    param(
        [string]$WorkspaceRoot,
        [string]$AppRoot,
        [ValidateSet('Debug', 'Release')]
        [string]$Configuration
    )

    if ([string]::IsNullOrWhiteSpace($AppRoot)) {
        $resolvedWorkspaceRoot = if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
            Get-DefaultWorkspaceRootFromTestRepo -TestRepoRoot $script:RepoRoot
        } else {
            [System.IO.Path]::GetFullPath($WorkspaceRoot)
        }

        $candidateRoots = @(
            (Join-Path $resolvedWorkspaceRoot 'app\eMule-main'),
            (Resolve-WorkspaceAppRoot -WorkspaceRoot $resolvedWorkspaceRoot -PreferredVariantNames @('main', 'build', 'bugfix'))
        )
        foreach ($candidateRoot in $candidateRoots) {
            if ($candidateRoot -and (Test-Path -LiteralPath $candidateRoot -PathType Container)) {
                $AppRoot = [System.IO.Path]::GetFullPath($candidateRoot)
                break
            }
        }
    }

    return [System.IO.Path]::GetFullPath((Join-Path $AppRoot ("srchybrid\x64\{0}\emule.exe" -f $Configuration)))
}

$script:RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$appExePath = Get-AppExecutablePath -WorkspaceRoot $WorkspaceRoot -AppRoot $AppRoot -Configuration $Configuration
if (-not (Test-Path -LiteralPath $appExePath -PathType Leaf)) {
    throw "App executable was not found at '$appExePath'."
}

$seedConfigDir = Join-Path $script:RepoRoot 'manifests\live-profile-seed\config'
if (-not (Test-Path -LiteralPath $seedConfigDir -PathType Container)) {
    throw "Seed config directory was not found at '$seedConfigDir'."
}

$pythonInvocation = Get-PythonInvocation
$pythonDependencyRoot = Join-Path $script:RepoRoot '.pydeps\shared-files-ui-e2e'
$pythonEnvironment = @{}

if (Test-Path -LiteralPath $pythonDependencyRoot -PathType Container) {
    $pythonEnvironment.PYTHONPATH = $pythonDependencyRoot
}

if (-not (Test-PythonImport -PythonInvocation $pythonInvocation -ModuleName 'pywinauto' -Environment $pythonEnvironment)) {
    $null = New-Item -ItemType Directory -Force -Path $pythonDependencyRoot
    Invoke-PythonChecked -PythonInvocation $pythonInvocation -Arguments @(
        '-m', 'pip', 'install',
        '--disable-pip-version-check',
        '--upgrade',
        '--target', $pythonDependencyRoot,
        'pywinauto'
    )
    $pythonEnvironment.PYTHONPATH = $pythonDependencyRoot
}

if ([string]::IsNullOrWhiteSpace($ArtifactsRoot)) {
    $ArtifactsRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('emule-shared-files-ui-e2e-' + [Guid]::NewGuid().ToString('N'))
}

$null = New-Item -ItemType Directory -Force -Path $ArtifactsRoot
$pythonScriptPath = Join-Path $PSScriptRoot 'shared-files-ui-e2e.py'

try {
    Invoke-PythonChecked -PythonInvocation $pythonInvocation -Environment $pythonEnvironment -Arguments @(
        $pythonScriptPath,
        '--app-exe', $appExePath,
        '--seed-config-dir', $seedConfigDir,
        '--artifacts-dir', $ArtifactsRoot
    )

    if ($KeepArtifacts) {
        Write-Host "Shared Files UI E2E passed. Artifacts kept at: $ArtifactsRoot"
    } else {
        Remove-Item -LiteralPath $ArtifactsRoot -Recurse -Force
        Write-Host 'Shared Files UI E2E passed.'
    }
}
catch {
    Write-Host "Shared Files UI E2E failed. Artifacts kept at: $ArtifactsRoot" -ForegroundColor Red
    throw
}
