#Requires -Version 7.6
<#
.SYNOPSIS
Resolves the canonical app worktree path from one workspace manifest.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-WorkspaceAppRoot {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkspaceRoot,

        [string[]]$PreferredVariantNames = @('bugfix', 'test', 'build')
    )

    $workspaceRootPath = (Resolve-Path -LiteralPath $WorkspaceRoot).Path
    $manifestPath = Join-Path $workspaceRootPath 'deps.psd1'
    $manifestCandidates = New-Object System.Collections.Generic.List[string]

    if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
        $manifest = Import-PowerShellDataFile -LiteralPath $manifestPath
        $seedPath = $manifest.Workspace.AppRepo.SeedRepo.Path
        if (-not [string]::IsNullOrWhiteSpace($seedPath)) {
            $manifestCandidates.Add((Join-Path $workspaceRootPath $seedPath))
        }

        foreach ($preferredVariantName in $PreferredVariantNames) {
            foreach ($variant in @($manifest.Workspace.AppRepo.Variants)) {
                if ($variant.Name -eq $preferredVariantName -and -not [string]::IsNullOrWhiteSpace($variant.Path)) {
                    $manifestCandidates.Add((Join-Path $workspaceRootPath $variant.Path))
                }
            }
        }

        foreach ($variant in @($manifest.Workspace.AppRepo.Variants)) {
            if (-not [string]::IsNullOrWhiteSpace($variant.Path)) {
                $manifestCandidates.Add((Join-Path $workspaceRootPath $variant.Path))
            }
        }
    }

    foreach ($candidate in @($manifestCandidates.ToArray())) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Container)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "Unable to resolve a canonical app root from '$manifestPath'."
}
