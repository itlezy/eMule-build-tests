#Requires -Version 7.2
<#
.SYNOPSIS
Builds and runs the shared eMule test binary in two workspaces and compares suite-level XML results.

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
        [string]$SuiteName,

        [Parameter(Mandatory = $true)]
        [string]$XmlPath,

        [Parameter(Mandatory = $true)]
        [string]$LogPath,

        [Parameter(Mandatory = $true)]
        [string]$ExitCodePath
    )

    $binaryPath = Join-Path $WorkspaceRoot ("tests\build\{0}\{1}\emule-tests.exe" -f $Platform, $Configuration)
    if (-not (Test-Path -LiteralPath $binaryPath)) {
        throw "Built test executable not found: $binaryPath"
    }

    $arguments = @(
        '--reporters=xml',
        '--no-intro',
        '--no-version',
        "--test-suite=$SuiteName",
        "--out=$XmlPath"
    )

    & $binaryPath @arguments *>&1 | Tee-Object -FilePath $LogPath
    Set-Content -LiteralPath $ExitCodePath -Value $LASTEXITCODE
}

function Invoke-Build {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkspaceRoot
    )

    $scriptPath = Join-Path $WorkspaceRoot 'tests\scripts\build-emule-tests.ps1'
    if (-not (Test-Path -LiteralPath $scriptPath)) {
        throw "Shared test build script not found: $scriptPath"
    }

    & $scriptPath -WorkspaceRoot $WorkspaceRoot -Configuration $Configuration -Platform $Platform
}

function Get-TestCaseResults {
    param(
        [Parameter(Mandatory = $true)]
        [string]$XmlPath,

        [Parameter(Mandatory = $true)]
        [string]$SuiteName,

        [Parameter(Mandatory = $true)]
        [string]$WorkspaceId
    )

    if (-not (Test-Path -LiteralPath $XmlPath)) {
        throw "Structured test result not found: $XmlPath"
    }

    [xml]$xml = Get-Content -Raw -LiteralPath $XmlPath
    $results = @{}
    foreach ($testSuite in @($xml.doctest.TestSuite)) {
        $parsedSuiteName = [string]$testSuite.name
        if (-not $parsedSuiteName) {
            $parsedSuiteName = $SuiteName
        }

        foreach ($testCase in @($testSuite.TestCase)) {
            $results[[string]$testCase.name] = [PSCustomObject]@{
                Workspace = $WorkspaceId
                Suite = $parsedSuiteName
                Name = [string]$testCase.name
                Success = ([string]$testCase.OverallResultsAsserts.test_case_success) -eq 'true'
                Failures = [int]$testCase.OverallResultsAsserts.failures
            }
        }
    }

    return $results
}

function Compare-CaseSets {
    param(
        [Parameter(Mandatory = $true)]
        [hashtable]$DevResults,

        [Parameter(Mandatory = $true)]
        [hashtable]$OracleResults,

        [Parameter(Mandatory = $true)]
        [string]$SuiteName,

        [Parameter(Mandatory = $true)]
        [System.Collections.Generic.List[string]]$SummaryLines
    )

    $allNames = @($DevResults.Keys + $OracleResults.Keys | Sort-Object -Unique)
    $hasFailure = $false

    foreach ($name in $allNames) {
        $devCase = $DevResults[$name]
        $oracleCase = $OracleResults[$name]
        if ($null -eq $devCase -or $null -eq $oracleCase) {
            $SummaryLines.Add(("[FAIL] {0}: case-set mismatch for '{1}'" -f $SuiteName, $name))
            $hasFailure = $true
            continue
        }

        if ($SuiteName -eq 'parity') {
            if ($devCase.Success -and $oracleCase.Success) {
                $SummaryLines.Add(("[PASS] parity: {0}" -f $name))
            } else {
                $SummaryLines.Add(("[FAIL] parity: {0} (dev={1}, oracle={2})" -f $name, $devCase.Success, $oracleCase.Success))
                $hasFailure = $true
            }
            continue
        }

        if ($devCase.Success -and -not $oracleCase.Success) {
            $SummaryLines.Add(("[PASS] divergence: {0} (dev pass, oracle fail as expected)" -f $name))
        } elseif (-not $devCase.Success) {
            $SummaryLines.Add(("[FAIL] divergence: {0} (dev failed)" -f $name))
            $hasFailure = $true
        } elseif ($oracleCase.Success) {
            $SummaryLines.Add(("[WARN] divergence: {0} (oracle also passed)" -f $name))
        } else {
            $SummaryLines.Add(("[FAIL] divergence: {0} (unexpected state dev={1}, oracle={2})" -f $name, $devCase.Success, $oracleCase.Success))
            $hasFailure = $true
        }
    }

    return $hasFailure
}

$reportRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'reports'
New-Item -ItemType Directory -Path $reportRoot -Force | Out-Null

Invoke-Build -WorkspaceRoot $DevWorkspaceRoot
Invoke-Build -WorkspaceRoot $OracleWorkspaceRoot

$summaryLines = [System.Collections.Generic.List[string]]::new()
$failed = $false

foreach ($suiteName in @('parity', 'divergence')) {
    $devXml = Join-Path $reportRoot ("dev-{0}.xml" -f $suiteName)
    $oracleXml = Join-Path $reportRoot ("oracle-{0}.xml" -f $suiteName)
    $devLog = Join-Path $reportRoot ("dev-{0}.log" -f $suiteName)
    $oracleLog = Join-Path $reportRoot ("oracle-{0}.log" -f $suiteName)
    $devExitCode = Join-Path $reportRoot ("dev-{0}-exit-code.txt" -f $suiteName)
    $oracleExitCode = Join-Path $reportRoot ("oracle-{0}-exit-code.txt" -f $suiteName)

    Invoke-TestRun -WorkspaceRoot $DevWorkspaceRoot -SuiteName $suiteName -XmlPath $devXml -LogPath $devLog -ExitCodePath $devExitCode
    Invoke-TestRun -WorkspaceRoot $OracleWorkspaceRoot -SuiteName $suiteName -XmlPath $oracleXml -LogPath $oracleLog -ExitCodePath $oracleExitCode

    $devResults = Get-TestCaseResults -XmlPath $devXml -SuiteName $suiteName -WorkspaceId 'dev'
    $oracleResults = Get-TestCaseResults -XmlPath $oracleXml -SuiteName $suiteName -WorkspaceId 'oracle'
    if (Compare-CaseSets -DevResults $devResults -OracleResults $oracleResults -SuiteName $suiteName -SummaryLines $summaryLines) {
        $failed = $true
    }
}

$summaryPath = Join-Path $reportRoot 'live-diff-summary.txt'
Set-Content -LiteralPath $summaryPath -Value $summaryLines
$summaryLines | ForEach-Object { Write-Output $_ }
Write-Output "Summary: $summaryPath"

if ($failed) {
    throw 'Live test comparison detected parity failures or unexpected dev regressions.'
}
