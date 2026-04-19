#Requires -Version 7.6
<#
.SYNOPSIS
Resolves canonical workspace, repo, and default path roots for shared harness scripts.
#>

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-EmuleWorkspaceRootFromTestRepo {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$TestRepoRoot
    )

    if (-not [string]::IsNullOrWhiteSpace($env:EMULE_WORKSPACE_ROOT)) {
        return [System.IO.Path]::GetFullPath($env:EMULE_WORKSPACE_ROOT)
    }

    $repoRootPath = (Resolve-Path -LiteralPath $TestRepoRoot).Path
    return [System.IO.Path]::GetFullPath((Join-Path $repoRootPath '..\..'))
}

function Get-DefaultWorkspaceRootFromTestRepo {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$TestRepoRoot,

        [string]$WorkspaceName = 'v0.72a'
    )

    $emuleWorkspaceRoot = Get-EmuleWorkspaceRootFromTestRepo -TestRepoRoot $TestRepoRoot
    return [System.IO.Path]::GetFullPath((Join-Path $emuleWorkspaceRoot ("workspaces\{0}" -f $WorkspaceName)))
}

function Get-DefaultRemoteRootFromTestRepo {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$TestRepoRoot
    )

    $emuleWorkspaceRoot = Get-EmuleWorkspaceRootFromTestRepo -TestRepoRoot $TestRepoRoot
    return [System.IO.Path]::GetFullPath((Join-Path $emuleWorkspaceRoot 'repos\eMule-remote'))
}
