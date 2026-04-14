#Requires -Version 7.6

param(
    [string]$WorkspaceRoot,
    [string]$AppRoot,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$ArtifactsRoot,
    [string]$SharedRoot = 'C:\tmp\00_long_paths',
    [string[]]$Scenario = @(),
    [switch]$KeepArtifacts
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'resolve-workspace-layout.ps1')
. (Join-Path $PSScriptRoot 'resolve-app-root.ps1')

function Publish-DirectorySnapshot {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourceDirectory,

        [Parameter(Mandatory = $true)]
        [string]$DestinationDirectory
    )

    if (Test-Path -LiteralPath $DestinationDirectory -PathType Container) {
        Remove-Item -LiteralPath $DestinationDirectory -Recurse -Force
    }

    New-Item -ItemType Directory -Force -Path $DestinationDirectory | Out-Null
    foreach ($entry in @(Get-ChildItem -LiteralPath $SourceDirectory -Force -ErrorAction SilentlyContinue)) {
        Copy-Item -LiteralPath $entry.FullName -Destination $DestinationDirectory -Recurse -Force
    }
}

function Read-JsonFile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }

    Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json -Depth 12
}

function Get-ReportToken {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    $token = ($Value -replace '[\\/:*?"<>|\s]+', '-') -replace '[^A-Za-z0-9._-]+', '-'
    $token = $token.Trim('-')
    if ([string]::IsNullOrWhiteSpace($token)) {
        return 'run'
    }

    return $token
}

function Get-AppVariantLabel {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$AppExecutablePath
    )

    $binaryDirectory = Split-Path -Parent $AppExecutablePath
    $appRoot = [System.IO.Path]::GetFullPath((Join-Path $binaryDirectory '..\..\..'))
    return Split-Path -Leaf $appRoot
}

function Write-StartupProfilesSummary {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$SummaryPath,

        [Parameter(Mandatory = $true)]
        [string]$Status,

        [Parameter(Mandatory = $true)]
        [string]$AppExecutablePath,

        [Parameter(Mandatory = $true)]
        [string]$Configuration,

        [Parameter(Mandatory = $true)]
        [string]$SharedRoot,

        [Parameter(Mandatory = $true)]
        [string]$ArtifactDirectory,

        [Parameter(Mandatory = $true)]
        [string]$LatestReportDirectory,

        [string]$ErrorMessage = '',

        [string]$SourceArtifactDirectory = ''
    )

    $resultPath = Join-Path $ArtifactDirectory 'startup-profiles-summary.json'
    $summary = [ordered]@{
        generated_at = (Get-Date).ToString('o')
        status = $Status
        app_exe = $AppExecutablePath
        configuration = $Configuration
        shared_root = $SharedRoot
        artifact_dir = $ArtifactDirectory
        latest_report_dir = $LatestReportDirectory
        source_artifact_dir = $SourceArtifactDirectory
        result = Read-JsonFile -Path $resultPath
        error = if ([string]::IsNullOrWhiteSpace($ErrorMessage)) { $null } else { $ErrorMessage }
    }

    $summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $SummaryPath -Encoding utf8
}

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
$pythonEnvironment = @{
    PYTHONPATH = $pythonDependencyRoot
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
}

$reportRoot = Join-Path $script:RepoRoot 'reports'
$profilesReportRoot = Join-Path $reportRoot 'startup-profile-scenarios'
$reportStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$reportLabel = "{0}-{1}-{2}" -f $reportStamp, (Get-ReportToken -Value (Get-AppVariantLabel -AppExecutablePath $appExePath)), $Configuration.ToLowerInvariant()
$runReportDir = Join-Path $profilesReportRoot $reportLabel
$latestReportDir = Join-Path $reportRoot 'startup-profile-scenarios-latest'
$sourceArtifactsRoot = if ([string]::IsNullOrWhiteSpace($ArtifactsRoot)) {
    Join-Path ([System.IO.Path]::GetTempPath()) ('emule-startup-profile-scenarios-' + [Guid]::NewGuid().ToString('N'))
} else {
    [System.IO.Path]::GetFullPath($ArtifactsRoot)
}
$retainSourceArtifacts = $KeepArtifacts -or -not [string]::IsNullOrWhiteSpace($ArtifactsRoot)

$null = New-Item -ItemType Directory -Force -Path $sourceArtifactsRoot
$pythonScriptPath = Join-Path $PSScriptRoot 'startup-profile-scenarios.py'
$runStatus = 'failed'
$errorMessage = ''

try {
    $pythonArguments = @(
        $pythonScriptPath,
        '--app-exe', $appExePath,
        '--seed-config-dir', $seedConfigDir,
        '--artifacts-dir', $sourceArtifactsRoot,
        '--shared-root', $SharedRoot
    )
    foreach ($scenarioName in $Scenario) {
        $pythonArguments += @('--scenario', $scenarioName)
    }
    Invoke-PythonChecked -PythonInvocation $pythonInvocation -Environment $pythonEnvironment -Arguments $pythonArguments
    $runStatus = 'passed'
}
catch {
    $errorMessage = $_.Exception.Message
}
finally {
    $null = New-Item -ItemType Directory -Force -Path $profilesReportRoot
    Publish-DirectorySnapshot -SourceDirectory $sourceArtifactsRoot -DestinationDirectory $runReportDir

    $summaryPath = Join-Path $runReportDir 'startup-profiles-wrapper-summary.json'
    Write-StartupProfilesSummary `
        -SummaryPath $summaryPath `
        -Status $runStatus `
        -AppExecutablePath $appExePath `
        -Configuration $Configuration `
        -SharedRoot $SharedRoot `
        -ArtifactDirectory $runReportDir `
        -LatestReportDirectory $latestReportDir `
        -ErrorMessage $errorMessage `
        -SourceArtifactDirectory $sourceArtifactsRoot

    Publish-DirectorySnapshot -SourceDirectory $runReportDir -DestinationDirectory $latestReportDir

    $publishHarnessSummaryPath = Join-Path $script:RepoRoot 'scripts\publish-harness-summary.ps1'
    & $publishHarnessSummaryPath -TestRepoRoot $script:RepoRoot -StartupProfileSummaryPath $summaryPath | Out-Null

    if (-not $retainSourceArtifacts -and (Test-Path -LiteralPath $sourceArtifactsRoot -PathType Container)) {
        Remove-Item -LiteralPath $sourceArtifactsRoot -Recurse -Force
    }
}

if ($runStatus -eq 'passed') {
    Write-Host "Startup-profile scenarios passed. Report directory: $runReportDir"
    return
}

Write-Host "Startup-profile scenarios failed. Report directory: $runReportDir" -ForegroundColor Red
throw $(if ([string]::IsNullOrWhiteSpace($errorMessage)) { 'Startup-profile scenarios failed.' } else { $errorMessage })
