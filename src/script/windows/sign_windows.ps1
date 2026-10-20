param(
    [Parameter(Mandatory = $true)]
    [string]$FilePath,

    [Parameter(Mandatory = $true)]
    [ValidateSet('LocalTest', 'Store', 'Artifact')]
    [string]$Mode,

    [string]$Thumbprint = $env:CORAL_SIGN_THUMBPRINT,
    [string]$TimestampUrl = $env:CORAL_SIGN_TIMESTAMP_URL,
    [string]$ArtifactDlib = $env:CORAL_SIGN_DLIB,
    [string]$ArtifactMetadata = $env:CORAL_SIGN_METADATA
)
& (Join-Path $PSScriptRoot "../../../packaging/windows/sign_windows.ps1") @PSBoundParameters
