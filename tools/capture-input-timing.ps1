param([Parameter(Mandatory=$true)][string]$OutputPath)
& (Join-Path $PSScriptRoot "diagnostics/capture-input-timing.ps1") @PSBoundParameters
