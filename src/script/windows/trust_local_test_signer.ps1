param([switch]$Remove)
& (Join-Path $PSScriptRoot "../../../packaging/windows/trust_local_test_signer.ps1") @PSBoundParameters
