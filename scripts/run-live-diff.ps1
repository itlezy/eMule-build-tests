#Requires -Version 7.6
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

    [string]$DevWorkspaceRoot,
    [string]$OracleWorkspaceRoot,
    [string]$DevAppRoot,
    [string]$OracleAppRoot,

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidateSet('x64')]
    [string]$Platform = 'x64'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'resolve-workspace-layout.ps1')

function Get-BuildTag {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkspaceRoot,

        [string]$AppRoot
    )

    $workspaceLeaf = Split-Path -Leaf $WorkspaceRoot
    $workspacesRoot = Split-Path -Parent $WorkspaceRoot
    $workspaceOwner = if ($workspacesRoot) { Split-Path -Leaf (Split-Path -Parent $workspacesRoot) } else { '' }
    if ([string]::IsNullOrWhiteSpace($workspaceLeaf) -or [string]::IsNullOrWhiteSpace($workspaceOwner)) {
        throw "Unable to derive build tag from workspace path: $WorkspaceRoot"
    }

    $segments = New-Object System.Collections.Generic.List[string]
    $segments.Add($workspaceOwner)
    $segments.Add($workspaceLeaf)
    if (-not [string]::IsNullOrWhiteSpace($AppRoot)) {
        $segments.Add((Split-Path -Leaf $AppRoot))
    }

    (($segments -join '-') -replace '[^A-Za-z0-9._-]', '_')
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

function Invoke-LiveDiffBuild {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkspaceRoot,

        [string]$AppRoot
    )

    $scriptPath = Join-Path $script:testRepoRootPath 'scripts\build-emule-tests.ps1'
    if (-not (Test-Path -LiteralPath $scriptPath)) {
        throw "Shared test build script not found: $scriptPath"
    }

    $buildTag = Get-BuildTag -WorkspaceRoot $WorkspaceRoot -AppRoot $AppRoot
    if (-not [string]::IsNullOrWhiteSpace($AppRoot)) {
        & $scriptPath `
            -TestRepoRoot $script:testRepoRootPath `
            -WorkspaceRoot $WorkspaceRoot `
            -AppRoot $AppRoot `
            -Configuration $Configuration `
            -Platform $Platform `
            -BuildTag $buildTag `
            -BuildOutputMode Full
        return
    }

    & $scriptPath `
        -TestRepoRoot $script:testRepoRootPath `
        -WorkspaceRoot $WorkspaceRoot `
        -Configuration $Configuration `
        -Platform $Platform `
        -BuildTag $buildTag `
        -BuildOutputMode Full
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
            [void]$script:summaryLines.Add(("[WARN] {0}: case-set mismatch for '{1}'" -f $SuiteName, $name))
            $summary.warn_count += 1
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
        } elseif (-not $devCase.Success -and -not $oracleCase.Success) {
            [void]$script:summaryLines.Add(("[WARN] divergence: {0} (dev and oracle both failed)" -f $name))
            $summary.warn_count += 1
        } elseif (-not $devCase.Success -and $oracleCase.Success) {
            [void]$script:summaryLines.Add(("[FAIL] divergence: {0} (dev failed while oracle passed)" -f $name))
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
$DevWorkspaceRoot = if ([string]::IsNullOrWhiteSpace($DevWorkspaceRoot)) {
    Get-DefaultWorkspaceRootFromTestRepo -TestRepoRoot $testRepoRootPath
} else {
    $DevWorkspaceRoot
}
if ([string]::IsNullOrWhiteSpace($OracleWorkspaceRoot)) {
    throw 'OracleWorkspaceRoot is required for live diff runs in the canonical repos/workspaces layout.'
}
$reportRoot = Join-Path $testRepoRootPath 'reports'
New-Item -ItemType Directory -Path $reportRoot -Force | Out-Null

Invoke-LiveDiffBuild -WorkspaceRoot $DevWorkspaceRoot -AppRoot $DevAppRoot
Invoke-LiveDiffBuild -WorkspaceRoot $OracleWorkspaceRoot -AppRoot $OracleAppRoot

$summaryLines = [System.Collections.ArrayList]::new()
$suiteSummaries = New-Object System.Collections.Generic.List[object]
$failed = $false

foreach ($suiteName in @('parity', 'divergence')) {
    $devBuildTag = Get-BuildTag -WorkspaceRoot $DevWorkspaceRoot -AppRoot $DevAppRoot
    $oracleBuildTag = Get-BuildTag -WorkspaceRoot $OracleWorkspaceRoot -AppRoot $OracleAppRoot
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
try {
    & $publishHarnessSummaryPath -TestRepoRoot $testRepoRootPath -LiveDiffSummaryPath $summaryJsonPath
} catch {
    throw 'Failed to publish the combined harness summary after the live diff run.'
}
$summaryLines | ForEach-Object { Write-Output $_ }
Write-Output "Summary: $summaryPath"

if ($failed) {
    throw 'Live test comparison detected parity failures or unexpected dev regressions.'
}
