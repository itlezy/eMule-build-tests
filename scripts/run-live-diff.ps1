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
    [string]$TestRepoRoot = (Split-Path -Parent $PSScriptRoot),

    [string]$DevWorkspaceRoot = 'C:\prj\p2p\eMule\eMulebb\eMule-build',
    [string]$OracleWorkspaceRoot = 'C:\prj\p2p\eMule\eMulebb\eMule-build-oracle-v0.72a-oracle',

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidateSet('Win32', 'x64')]
    [string]$Platform = 'x64'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-BuildTag {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkspaceRoot
    )

    $leaf = Split-Path -Leaf $WorkspaceRoot
    if ([string]::IsNullOrWhiteSpace($leaf)) {
        throw "Unable to derive build tag from workspace path: $WorkspaceRoot"
    }

    ($leaf -replace '[^A-Za-z0-9._-]', '_')
}

function Invoke-TestRun {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkspaceRoot,

        [Parameter(Mandatory = $true)]
        [string]$BuildTag,

        [Parameter(Mandatory = $true)]
        [string]$SuiteName,

        [Parameter(Mandatory = $true)]
        [string]$XmlPath,

        [Parameter(Mandatory = $true)]
        [string]$LogPath,

        [Parameter(Mandatory = $true)]
        [string]$ExitCodePath
    )

    $binaryPath = Join-Path $script:testRepoRootPath ("build\{0}\{1}\{2}\emule-tests.exe" -f $BuildTag, $Platform, $Configuration)
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

    $scriptPath = Join-Path $script:testRepoRootPath 'scripts\build-emule-tests.ps1'
    if (-not (Test-Path -LiteralPath $scriptPath)) {
        throw "Shared test build script not found: $scriptPath"
    }

    $buildTag = Get-BuildTag -WorkspaceRoot $WorkspaceRoot
    & $scriptPath -TestRepoRoot $script:testRepoRootPath -WorkspaceRoot $WorkspaceRoot -Configuration $Configuration -Platform $Platform -BuildTag $buildTag
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

        if ($parsedSuiteName -ne $SuiteName) {
            continue
        }

        foreach ($testCase in @($testSuite.TestCase)) {
            $overall = $testCase.OverallResultsAsserts
            $isSkipped = ($testCase.GetAttribute('skipped')) -eq 'true'
            $results[[string]$testCase.name] = [PSCustomObject]@{
                Workspace = $WorkspaceId
                Suite = $parsedSuiteName
                Name = [string]$testCase.name
                Success = $null -ne $overall -and ([string]$overall.test_case_success) -eq 'true'
                Failures = if ($null -ne $overall) { [int]$overall.failures } else { 0 }
                Skipped = $isSkipped
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
        [string]$SuiteName
    )

    $allNames = @($DevResults.Keys + $OracleResults.Keys | Sort-Object -Unique)
    $hasFailure = $false
    $summary = [ordered]@{
        suite_name = $SuiteName
        total_cases = $allNames.Count
        pass_count = 0
        warn_count = 0
        fail_count = 0
        case_set_mismatch_count = 0
    }

    foreach ($name in $allNames) {
        $devCase = $DevResults[$name]
        $oracleCase = $OracleResults[$name]
        if ($null -eq $devCase -or $null -eq $oracleCase) {
            [void]$script:summaryLines.Add(("[FAIL] {0}: case-set mismatch for '{1}'" -f $SuiteName, $name))
            $hasFailure = $true
            $summary.fail_count += 1
            $summary.case_set_mismatch_count += 1
            continue
        }

        if ($SuiteName -eq 'parity') {
            if ($devCase.Success -and $oracleCase.Success) {
                [void]$script:summaryLines.Add(("[PASS] parity: {0}" -f $name))
                $summary.pass_count += 1
            } else {
                [void]$script:summaryLines.Add(("[FAIL] parity: {0} (dev={1}, oracle={2})" -f $name, $devCase.Success, $oracleCase.Success))
                $hasFailure = $true
                $summary.fail_count += 1
            }
            continue
        }

        if ($devCase.Success -and -not $oracleCase.Success) {
            [void]$script:summaryLines.Add(("[PASS] divergence: {0} (dev pass, oracle fail as expected)" -f $name))
            $summary.pass_count += 1
        } elseif (-not $devCase.Success) {
            [void]$script:summaryLines.Add(("[FAIL] divergence: {0} (dev failed)" -f $name))
            $hasFailure = $true
            $summary.fail_count += 1
        } elseif ($oracleCase.Success) {
            [void]$script:summaryLines.Add(("[WARN] divergence: {0} (oracle also passed)" -f $name))
            $summary.warn_count += 1
        } else {
            [void]$script:summaryLines.Add(("[FAIL] divergence: {0} (unexpected state dev={1}, oracle={2})" -f $name, $devCase.Success, $oracleCase.Success))
            $hasFailure = $true
            $summary.fail_count += 1
        }
    }

    return [pscustomobject]@{
        has_failure = $hasFailure
        summary = $summary
    }
}

$testRepoRootPath = (Resolve-Path -LiteralPath $TestRepoRoot).Path
$reportRoot = Join-Path $testRepoRootPath 'reports'
New-Item -ItemType Directory -Path $reportRoot -Force | Out-Null

Invoke-Build -WorkspaceRoot $DevWorkspaceRoot
Invoke-Build -WorkspaceRoot $OracleWorkspaceRoot

$summaryLines = [System.Collections.ArrayList]::new()
$suiteSummaries = New-Object System.Collections.Generic.List[object]
$failed = $false

foreach ($suiteName in @('parity', 'divergence')) {
    $devBuildTag = Get-BuildTag -WorkspaceRoot $DevWorkspaceRoot
    $oracleBuildTag = Get-BuildTag -WorkspaceRoot $OracleWorkspaceRoot
    $devXml = Join-Path $reportRoot ("dev-{0}.xml" -f $suiteName)
    $oracleXml = Join-Path $reportRoot ("oracle-{0}.xml" -f $suiteName)
    $devLog = Join-Path $reportRoot ("dev-{0}.log" -f $suiteName)
    $oracleLog = Join-Path $reportRoot ("oracle-{0}.log" -f $suiteName)
    $devExitCode = Join-Path $reportRoot ("dev-{0}-exit-code.txt" -f $suiteName)
    $oracleExitCode = Join-Path $reportRoot ("oracle-{0}-exit-code.txt" -f $suiteName)

    Invoke-TestRun -WorkspaceRoot $DevWorkspaceRoot -BuildTag $devBuildTag -SuiteName $suiteName -XmlPath $devXml -LogPath $devLog -ExitCodePath $devExitCode
    Invoke-TestRun -WorkspaceRoot $OracleWorkspaceRoot -BuildTag $oracleBuildTag -SuiteName $suiteName -XmlPath $oracleXml -LogPath $oracleLog -ExitCodePath $oracleExitCode

    $devResults = Get-TestCaseResults -XmlPath $devXml -SuiteName $suiteName -WorkspaceId 'dev'
    $oracleResults = Get-TestCaseResults -XmlPath $oracleXml -SuiteName $suiteName -WorkspaceId 'oracle'
    $comparison = Compare-CaseSets -DevResults $devResults -OracleResults $oracleResults -SuiteName $suiteName
    $suiteSummaries.Add($comparison.summary)
    if ($comparison.has_failure) {
        $failed = $true
    }
}

$summaryPath = Join-Path $reportRoot 'live-diff-summary.txt'
Set-Content -LiteralPath $summaryPath -Value $summaryLines
$summaryJsonPath = Join-Path $reportRoot 'live-diff-summary.json'
[ordered]@{
    generated_at = (Get-Date).ToString('o')
    report_root = $reportRoot
    dev_workspace_root = $DevWorkspaceRoot
    oracle_workspace_root = $OracleWorkspaceRoot
    configuration = $Configuration
    platform = $Platform
    suites = @($suiteSummaries.ToArray())
    failed = $failed
    text_summary_path = $summaryPath
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryJsonPath -Encoding utf8
$publishHarnessSummaryPath = Join-Path $testRepoRootPath 'scripts\publish-harness-summary.ps1'
& $publishHarnessSummaryPath -TestRepoRoot $testRepoRootPath -LiveDiffSummaryPath $summaryJsonPath
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to publish the combined harness summary after the live diff run.'
}
$summaryLines | ForEach-Object { Write-Output $_ }
Write-Output "Summary: $summaryPath"

if ($failed) {
    throw 'Live test comparison detected parity failures or unexpected dev regressions.'
}
