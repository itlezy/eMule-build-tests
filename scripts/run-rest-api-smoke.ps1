#Requires -Version 7.6

param(
    [string]$WorkspaceRoot,
    [string]$AppRoot,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$ArtifactsRoot,
    [string]$ApiKey = 'rest-smoke-test-key',
    [AllowEmptyString()]
    [string]$BindAddr = '127.0.0.1',
    [switch]$EnableUPnP,
    [string]$P2PBindInterfaceName,
    [string]$BindUpdaterScript = 'C:\prj\p2p\eMule\eMulebb-workspace\repos\eMule-tooling\scripts\config-bindaddr-updater.ps1',
    [double]$RestReadyTimeoutSeconds = 45.0,
    [double]$ServerActivityTimeoutSeconds = 45.0,
    [double]$KadRunningTimeoutSeconds = 30.0,
    [double]$NetworkReadyTimeoutSeconds = 120.0,
    [double]$SearchObservationTimeoutSeconds = 30.0,
    [ValidateRange(0, 100)]
    [int]$ServerSearchCount = 0,
    [ValidateRange(0, 100)]
    [int]$KadSearchCount = 0,
    [ValidateSet('automatic', 'server', 'global', 'kad')]
    [string]$SearchMethodOverride,
    [switch]$KeepArtifacts,
    [switch]$KeepRunning
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
    $appRootPath = [System.IO.Path]::GetFullPath((Join-Path $binaryDirectory '..\..\..'))
    return Split-Path -Leaf $appRootPath
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
$pythonDependencyRoot = Join-Path $script:RepoRoot '.pydeps\shared-live-ui'
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
$smokeReportRoot = Join-Path $reportRoot 'rest-api-smoke'
$reportStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$reportLabel = "{0}-{1}-{2}" -f $reportStamp, (Get-ReportToken -Value (Get-AppVariantLabel -AppExecutablePath $appExePath)), $Configuration.ToLowerInvariant()
$runReportDir = Join-Path $smokeReportRoot $reportLabel
$latestReportDir = Join-Path $reportRoot 'rest-api-smoke-latest'
$sourceArtifactsRoot = if ([string]::IsNullOrWhiteSpace($ArtifactsRoot)) {
    Join-Path ([System.IO.Path]::GetTempPath()) ('emule-rest-api-smoke-' + [Guid]::NewGuid().ToString('N'))
} else {
    [System.IO.Path]::GetFullPath($ArtifactsRoot)
}
$retainSourceArtifacts = $KeepArtifacts -or $KeepRunning -or -not [string]::IsNullOrWhiteSpace($ArtifactsRoot)

$null = New-Item -ItemType Directory -Force -Path $sourceArtifactsRoot
$pythonScriptPath = Join-Path $PSScriptRoot 'rest-api-smoke.py'
$runStatus = 'failed'
$errorMessage = ''

try {
    $pythonArguments = @(
        $pythonScriptPath,
        '--app-exe', $appExePath,
        '--seed-config-dir', $seedConfigDir,
        '--artifacts-dir', $sourceArtifactsRoot,
        '--api-key', $ApiKey,
        '--rest-ready-timeout-seconds', [string]$RestReadyTimeoutSeconds,
        '--server-activity-timeout-seconds', [string]$ServerActivityTimeoutSeconds,
        '--kad-running-timeout-seconds', [string]$KadRunningTimeoutSeconds,
        '--network-ready-timeout-seconds', [string]$NetworkReadyTimeoutSeconds,
        '--search-observation-timeout-seconds', [string]$SearchObservationTimeoutSeconds,
        '--server-search-count', [string]$ServerSearchCount,
        '--kad-search-count', [string]$KadSearchCount
    )
    if ([string]::IsNullOrEmpty($BindAddr)) {
        $pythonArguments += '--bind-addr='
    } else {
        $pythonArguments += @('--bind-addr', $BindAddr)
    }
    if ($EnableUPnP) {
        $pythonArguments += '--enable-upnp'
    }
    if (-not [string]::IsNullOrWhiteSpace($P2PBindInterfaceName)) {
        $pythonArguments += @('--p2p-bind-interface-name', $P2PBindInterfaceName)
        $pythonArguments += @('--bind-updater-script', $BindUpdaterScript)
    }
    if (-not [string]::IsNullOrWhiteSpace($SearchMethodOverride)) {
        $pythonArguments += @('--search-method-override', $SearchMethodOverride)
    }
    if ($KeepRunning) {
        $pythonArguments += '--keep-running'
    }
    Invoke-PythonChecked -PythonInvocation $pythonInvocation -Environment $pythonEnvironment -Arguments $pythonArguments
    $runStatus = 'passed'
}
catch {
    $errorMessage = $_.Exception.Message
}
finally {
    $null = New-Item -ItemType Directory -Force -Path $smokeReportRoot
    Publish-DirectorySnapshot -SourceDirectory $sourceArtifactsRoot -DestinationDirectory $runReportDir
    Publish-DirectorySnapshot -SourceDirectory $runReportDir -DestinationDirectory $latestReportDir

    if (-not $retainSourceArtifacts -and (Test-Path -LiteralPath $sourceArtifactsRoot -PathType Container)) {
        Remove-Item -LiteralPath $sourceArtifactsRoot -Recurse -Force
    }
}

if ($runStatus -eq 'passed') {
    Write-Host "REST API live E2E passed. Report directory: $runReportDir"
    if ($KeepRunning) {
        $resultPath = Join-Path $runReportDir 'result.json'
        if (Test-Path -LiteralPath $resultPath -PathType Leaf) {
            $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
            $processId = $result.cleanup.process_id
            $profileBase = $result.cleanup.profile_base
            Write-Host "eMule left running. PID: $processId Profile: $profileBase"
        }
    }
    return
}

Write-Host "REST API live E2E failed. Report directory: $runReportDir" -ForegroundColor Red
throw $(if ([string]::IsNullOrWhiteSpace($errorMessage)) { 'REST API live E2E failed.' } else { $errorMessage })
