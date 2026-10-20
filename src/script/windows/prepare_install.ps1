param(
    [string]$PreviousInstallDir = '',
    [string]$PreviousProductRoot = '',
    [string]$TargetInstallDir = '',
    [switch]$ProcessesOnly
)
& (Join-Path $PSScriptRoot "../../../packaging/windows/prepare_install.ps1") @PSBoundParameters
