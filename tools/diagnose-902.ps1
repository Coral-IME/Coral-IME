param([Parameter(Mandatory=$true)][string]$OutputPath, [string]$Thumbprint = "")
& (Join-Path $PSScriptRoot "diagnostics/diagnose-902.ps1") @PSBoundParameters
