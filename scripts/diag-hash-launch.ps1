#Requires -Version 7.6
<#
.SYNOPSIS
    Launches eMule under diagnostic monitoring to investigate hashing hangs.
.DESCRIPTION
    Starts eMule with the testing profile, attaches procdump for automatic
    dump capture on sustained high CPU, and tails the verbose log for
    hashing progress.
.NOTES
    Designed for the videodupez hashing investigation (2026-04-01).
#>
[CmdletBinding()]
param(
    [string]$EmuleExe = '',
    [string]$ConfigDir = '',
    [string]$ReportDir = '',
    [int]$TimeoutSeconds = 180,
    [int]$CpuThresholdPercent = 90,
    [int]$CpuDurationSeconds = 30
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'resolve-workspace-layout.ps1')

$testRepoRootPath = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($ReportDir)) {
    $ReportDir = Join-Path $testRepoRootPath 'reports'
}
if ([string]::IsNullOrWhiteSpace($ConfigDir)) {
    $ConfigDir = Join-Path $ReportDir 'diag-hash-profile'
}
if ([string]::IsNullOrWhiteSpace($EmuleExe)) {
    $workspaceRoot = Get-DefaultWorkspaceRootFromTestRepo -TestRepoRoot $testRepoRootPath
    $EmuleExe = Join-Path $workspaceRoot 'app\eMule-v0.72a-bugfix\srchybrid\x64\Debug\emule.exe'
}

# --- Prepare report directory ---
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = Join-Path $ReportDir "diag-hash-$timestamp"
New-Item -ItemType Directory -Path $runDir -Force | Out-Null
Write-Host "[diag] Report directory: $runDir"

# --- Verify eMule executable ---
if (-not (Test-Path $EmuleExe)) {
    Write-Error "eMule executable not found: $EmuleExe"
    return
}

# --- Clear logs for clean capture ---
$logDir = Join-Path $ConfigDir 'logs'
if (Test-Path $logDir) {
    Get-ChildItem -Path $logDir -Filter '*.log' | Remove-Item -Force
}

# --- Launch eMule with explicit config root and VPN binding ---
Write-Host "[diag] Launching eMule..."
$emuleArgs = @('-c', $ConfigDir)
$emuleProc = Start-Process -FilePath $EmuleExe -ArgumentList $emuleArgs -PassThru
Write-Host "[diag] eMule PID: $($emuleProc.Id)"

# --- Give eMule a moment to initialize ---
Start-Sleep -Seconds 3

# --- Launch procdump to capture dump on sustained high CPU ---
$dumpPath = Join-Path $runDir 'emule-cpu.dmp'
$procdumpArgs = @(
    '-accepteula',
    '-ma',                          # full memory dump
    '-c', $CpuThresholdPercent,     # CPU threshold
    '-s', $CpuDurationSeconds,      # sustained duration
    '-n', '1',                      # capture 1 dump
    $emuleProc.Id,
    $dumpPath
)

Write-Host "[diag] Starting procdump (CPU > $CpuThresholdPercent% for $($CpuDurationSeconds)s triggers dump)..."
$procdumpProc = Start-Process -FilePath 'procdump' -ArgumentList $procdumpArgs -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $runDir 'procdump-stdout.txt') -RedirectStandardError (Join-Path $runDir 'procdump-stderr.txt')

# --- Monitor hashing progress via verbose log ---
$verboseLog = Join-Path $logDir 'eMule_Verbose.log'
$mainLog = Join-Path $logDir 'eMule.log'
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$lastLogSize = 0
$hashingStarted = $false
$hashingCompleted = $false

Write-Host "[diag] Monitoring for ${TimeoutSeconds}s (deadline: $($deadline.ToString('HH:mm:ss')))..."
Write-Host ""

while ((Get-Date) -lt $deadline) {
    # Check if eMule is still running
    if ($emuleProc.HasExited) {
        Write-Host "[diag] eMule exited with code $($emuleProc.ExitCode)"
        break
    }

    # Read main log for hashing events
    if (Test-Path $mainLog) {
        try {
            $logContent = [System.IO.File]::ReadAllText($mainLog)
            $logLines = $logContent -split [Environment]::NewLine

            foreach ($line in $logLines) {
                if ($line -match 'Hashing file:') {
                    if (-not $hashingStarted) {
                        $hashingStarted = $true
                        Write-Host "[diag] HASHING STARTED"
                    }
                    Write-Host "  LOG: $line"
                }
                if ($line -match 'hash.*new files') {
                    Write-Host "  LOG: $line"
                }
            }
        } catch {
            # Log may be locked
        }
    }

    # Read verbose log for checkpoints
    if (Test-Path $verboseLog) {
        try {
            $verboseContent = [System.IO.File]::ReadAllText($verboseLog)
            $currentSize = $verboseContent.Length
            if ($currentSize -gt $lastLogSize) {
                $newContent = $verboseContent.Substring($lastLogSize)
                $newLines = $newContent -split [Environment]::NewLine
                foreach ($line in $newLines) {
                    if ($line -match 'CreateFromFile checkpoint|Successfully saved AICH|raw-hash-complete|Hashing file') {
                        Write-Host "  VERBOSE: $line"
                    }
                }
                $lastLogSize = $currentSize
            }
        } catch {
            # Log may be locked
        }
    }

    # Sample CPU usage
    try {
        $cpuSample = Get-Process -Id $emuleProc.Id -ErrorAction SilentlyContinue
        if ($cpuSample) {
            $ws = [math]::Round($cpuSample.WorkingSet64 / 1MB, 1)
            $cpu = [math]::Round($cpuSample.CPU, 1)
            # Only print periodic status
        }
    } catch {
        # Process may have exited
    }

    Start-Sleep -Seconds 2
}

# --- Collect results ---
Write-Host ""
Write-Host "[diag] Timeout reached or eMule exited. Collecting results..."

# Copy logs
if (Test-Path $logDir) {
    Copy-Item -Path (Join-Path $logDir '*') -Destination $runDir -Force -ErrorAction SilentlyContinue
}

# Check if procdump captured anything
if (-not $procdumpProc.HasExited) {
    Write-Host "[diag] Stopping procdump..."
    Stop-Process -Id $procdumpProc.Id -Force -ErrorAction SilentlyContinue
}

$dumpCaptured = Test-Path $dumpPath
Write-Host "[diag] Dump captured: $dumpCaptured"

# Build summary
$summary = @{
    timestamp       = $timestamp
    emulePid        = $emuleProc.Id
    emuleExited     = $emuleProc.HasExited
    emuleExitCode   = if ($emuleProc.HasExited) { $emuleProc.ExitCode } else { $null }
    hashingStarted  = $hashingStarted
    dumpCaptured    = $dumpCaptured
    dumpPath        = if ($dumpCaptured) { $dumpPath } else { $null }
    reportDir       = $runDir
}

$summary | ConvertTo-Json -Depth 3 | Set-Content -Path (Join-Path $runDir 'summary.json') -Encoding UTF8
Write-Host "[diag] Summary written to: $(Join-Path $runDir 'summary.json')"

if (-not $emuleProc.HasExited) {
    Write-Host "[diag] eMule is still running (PID $($emuleProc.Id)). Check the dump or attach a debugger."
    Write-Host "[diag] To take a manual dump: procdump -accepteula -ma $($emuleProc.Id) $runDir\manual.dmp"
}
