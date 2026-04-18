#Requires -Version 7.6
<#
.SYNOPSIS
Launches eMule under diagnostic monitoring to investigate hash-heavy stalls.

.DESCRIPTION
Starts one debug `emule.exe` against an explicit isolated `-c` profile, attaches
`procdump` for automatic dump capture on sustained high CPU, tails the standard
logs for hashing progress, and publishes one stable `reports\diag-hash-latest`
pointer for the companion dump-analysis utilities.
#>
[CmdletBinding()]
param(
    [string]$WorkspaceRoot,
    [string]$AppRoot,
    [string]$EmuleExe = '',
    [string]$SeedConfigDir = '',
    [string]$ProfileRoot = '',
    [string]$ReportRoot = '',
    [int]$TimeoutSeconds = 180,
    [int]$CpuThresholdPercent = 90,
    [int]$CpuDurationSeconds = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'resolve-app-root.ps1')
. (Join-Path $PSScriptRoot 'resolve-workspace-layout.ps1')

function Publish-LatestDirectoryPointer {
    param(
        [Parameter(Mandatory = $true)]
        [string]$TargetDirectory,

        [Parameter(Mandatory = $true)]
        [string]$LatestDirectory
    )

    $latestParentDirectory = Split-Path -Parent $LatestDirectory
    if (-not [string]::IsNullOrWhiteSpace($latestParentDirectory)) {
        $null = New-Item -ItemType Directory -Force -Path $latestParentDirectory
    }

    if (Test-Path -LiteralPath $LatestDirectory) {
        Remove-Item -LiteralPath $LatestDirectory -Recurse -Force
    }

    $null = New-Item -ItemType Junction -Path $LatestDirectory -Target $TargetDirectory -Force
}

function Copy-SeedProfile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SeedConfigDirectory,

        [Parameter(Mandatory = $true)]
        [string]$ProfileBasePath
    )

    if (-not (Test-Path -LiteralPath $SeedConfigDirectory -PathType Container)) {
        throw "Seed config directory '$SeedConfigDirectory' does not exist."
    }

    $configDirectory = Join-Path $ProfileBasePath 'config'
    $logsDirectory = Join-Path $ProfileBasePath 'logs'
    $null = New-Item -ItemType Directory -Force -Path $configDirectory
    $null = New-Item -ItemType Directory -Force -Path $logsDirectory

    foreach ($seedEntry in @(Get-ChildItem -LiteralPath $SeedConfigDirectory -Force -ErrorAction SilentlyContinue)) {
        Copy-Item -LiteralPath $seedEntry.FullName -Destination $configDirectory -Recurse -Force
    }
}

$testRepoRootPath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$workspaceRootPath = if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    Get-DefaultWorkspaceRootFromTestRepo -TestRepoRoot $testRepoRootPath
} else {
    (Resolve-Path -LiteralPath $WorkspaceRoot).Path
}
$appRootPath = if ([string]::IsNullOrWhiteSpace($AppRoot)) {
    Resolve-WorkspaceAppRoot -WorkspaceRoot $workspaceRootPath -PreferredVariantNames @('main', 'build', 'bugfix')
} else {
    (Resolve-Path -LiteralPath $AppRoot).Path
}
if ([string]::IsNullOrWhiteSpace($EmuleExe)) {
    $EmuleExe = Join-Path $appRootPath 'srchybrid\x64\Debug\emule.exe'
}
$EmuleExe = [System.IO.Path]::GetFullPath($EmuleExe)

if ([string]::IsNullOrWhiteSpace($ReportRoot)) {
    $ReportRoot = Join-Path $testRepoRootPath 'reports\diag-hash'
}
$ReportRoot = [System.IO.Path]::GetFullPath($ReportRoot)
$latestReportDir = Join-Path (Split-Path -Parent $ReportRoot) 'diag-hash-latest'

if ([string]::IsNullOrWhiteSpace($SeedConfigDir)) {
    $SeedConfigDir = Join-Path $testRepoRootPath 'manifests\live-profile-seed\config'
}
$SeedConfigDir = [System.IO.Path]::GetFullPath($SeedConfigDir)

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = Join-Path $ReportRoot $timestamp
$null = New-Item -ItemType Directory -Path $runDir -Force

$profileRootPath = if ([string]::IsNullOrWhiteSpace($ProfileRoot)) {
    Join-Path $runDir 'profile-base'
} else {
    [System.IO.Path]::GetFullPath($ProfileRoot)
}

Write-Host "[diag] Report directory: $runDir"
Write-Host "[diag] App root: $appRootPath"
Write-Host "[diag] Profile root: $profileRootPath"

if (-not (Test-Path -LiteralPath $EmuleExe -PathType Leaf)) {
    throw "eMule executable not found: $EmuleExe"
}

Copy-SeedProfile -SeedConfigDirectory $SeedConfigDir -ProfileBasePath $profileRootPath

$logDir = Join-Path $profileRootPath 'logs'
foreach ($logFile in @(Get-ChildItem -LiteralPath $logDir -Filter '*.log' -ErrorAction SilentlyContinue)) {
    Remove-Item -LiteralPath $logFile.FullName -Force
}

Write-Host "[diag] Launching eMule..."
$emuleArgs = @('-ignoreinstances', '-c', $profileRootPath)
$emuleProc = Start-Process -FilePath $EmuleExe -ArgumentList $emuleArgs -PassThru
Write-Host "[diag] eMule PID: $($emuleProc.Id)"

Start-Sleep -Seconds 3

$dumpPath = Join-Path $runDir 'emule-cpu.dmp'
$procdumpArgs = @(
    '-accepteula',
    '-ma',
    '-c', $CpuThresholdPercent,
    '-s', $CpuDurationSeconds,
    '-n', '1',
    $emuleProc.Id,
    $dumpPath
)

Write-Host "[diag] Starting procdump (CPU > $CpuThresholdPercent% for $CpuDurationSeconds s triggers dump)..."
$procdumpProc = Start-Process -FilePath 'procdump' -ArgumentList $procdumpArgs -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $runDir 'procdump-stdout.txt') -RedirectStandardError (Join-Path $runDir 'procdump-stderr.txt')

$mainLog = Join-Path $logDir 'eMule.log'
$verboseLog = Join-Path $logDir 'eMule_Verbose.log'
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$lastVerboseLogSize = 0
$hashingStarted = $false

Write-Host "[diag] Monitoring for $TimeoutSeconds s (deadline: $($deadline.ToString('HH:mm:ss')))..."
Write-Host ''

while ((Get-Date) -lt $deadline) {
    if ($emuleProc.HasExited) {
        Write-Host "[diag] eMule exited with code $($emuleProc.ExitCode)"
        break
    }

    if (Test-Path -LiteralPath $mainLog -PathType Leaf) {
        try {
            $logContent = [System.IO.File]::ReadAllText($mainLog)
            foreach ($line in ($logContent -split [Environment]::NewLine)) {
                if ($line -match 'Hashing file:') {
                    if (-not $hashingStarted) {
                        $hashingStarted = $true
                        Write-Host '[diag] HASHING STARTED'
                    }
                    Write-Host "  LOG: $line"
                }
                if ($line -match 'hash.*new files') {
                    Write-Host "  LOG: $line"
                }
            }
        } catch {
        }
    }

    if (Test-Path -LiteralPath $verboseLog -PathType Leaf) {
        try {
            $verboseContent = [System.IO.File]::ReadAllText($verboseLog)
            if ($verboseContent.Length -gt $lastVerboseLogSize) {
                $newContent = $verboseContent.Substring($lastVerboseLogSize)
                foreach ($line in ($newContent -split [Environment]::NewLine)) {
                    if ($line -match 'CreateFromFile checkpoint|Successfully saved AICH|raw-hash-complete|Hashing file') {
                        Write-Host "  VERBOSE: $line"
                    }
                }
                $lastVerboseLogSize = $verboseContent.Length
            }
        } catch {
        }
    }

    Start-Sleep -Seconds 2
}

Write-Host ''
Write-Host '[diag] Collecting results...'

if (-not $procdumpProc.HasExited) {
    Write-Host '[diag] Stopping procdump...'
    Stop-Process -Id $procdumpProc.Id -Force -ErrorAction SilentlyContinue
}

$dumpCaptured = Test-Path -LiteralPath $dumpPath -PathType Leaf
Write-Host "[diag] Dump captured: $dumpCaptured"

try {
    Publish-LatestDirectoryPointer -TargetDirectory $runDir -LatestDirectory $latestReportDir
} catch {
    Write-Warning "Failed to refresh latest report pointer '$latestReportDir': $($_.Exception.Message)"
}

$summaryPath = Join-Path $runDir 'summary.json'
$summary = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    app_exe = $EmuleExe
    app_root = $appRootPath
    workspace_root = $workspaceRootPath
    profile_root = $profileRootPath
    seed_config_dir = $SeedConfigDir
    report_dir = $runDir
    latest_report_dir = $latestReportDir
    emule_pid = $emuleProc.Id
    emule_exited = $emuleProc.HasExited
    emule_exit_code = if ($emuleProc.HasExited) { $emuleProc.ExitCode } else { $null }
    hashing_started = $hashingStarted
    dump_captured = $dumpCaptured
    dump_path = if ($dumpCaptured) { $dumpPath } else { $null }
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding utf8
Write-Host "[diag] Summary written to: $summaryPath"

if (-not $emuleProc.HasExited) {
    Write-Host "[diag] eMule is still running (PID $($emuleProc.Id))."
    Write-Host "[diag] To take a manual dump: procdump -accepteula -ma $($emuleProc.Id) $(Join-Path $runDir 'manual.dmp')"
}
