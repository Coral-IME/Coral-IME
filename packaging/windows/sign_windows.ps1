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

$ErrorActionPreference = 'Stop'

function Get-MyCertificates($location) {
    $store = [System.Security.Cryptography.X509Certificates.X509Store]::new(
        'My', [System.Security.Cryptography.X509Certificates.StoreLocation]::$location)
    try {
        $store.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly)
        return @($store.Certificates)
    }
    finally { $store.Close() }
}

function Find-SignTool {
    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $sdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (-not (Test-Path -LiteralPath $sdkBin)) {
        throw 'Windows SDK SignTool was not found.'
    }
    $candidates = @(Get-ChildItem -LiteralPath $sdkBin -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName 'x64\signtool.exe' } |
        Where-Object { Test-Path -LiteralPath $_ })
    if (-not $candidates) { throw 'Windows SDK SignTool was not found.' }
    return $candidates[0]
}

function Get-LocalTestCertificate {
    $subject = 'CN=Coral Pinyin Local Test Code Signing'
    $certificate = Get-MyCertificates 'CurrentUser' |
        Where-Object {
            $_.Subject -eq $subject -and $_.HasPrivateKey -and
            $_.NotAfter -gt (Get-Date).AddDays(7)
        } |
        Sort-Object NotAfter -Descending |
        Select-Object -First 1
    if (-not $certificate) {
        $certificate = New-SelfSignedCertificate -Type CodeSigningCert `
            -Subject $subject -FriendlyName 'Coral Pinyin local test signing' `
            -CertStoreLocation Cert:\CurrentUser\My -KeyAlgorithm RSA `
            -KeyLength 3072 -KeyExportPolicy NonExportable `
            -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(1)
        Write-Host 'Created a non-exportable local test signing key in CurrentUser\My.'
    }
    return $certificate
}

$signTool = Find-SignTool
$item = Get-Item -LiteralPath $FilePath -ErrorAction Stop
$nsisUninstaller = $item.Name -match '^nst[0-9A-F]+\.tmp$' -and
    $item.DirectoryName.StartsWith([IO.Path]::GetTempPath().TrimEnd('\'),
        [StringComparison]::OrdinalIgnoreCase)
if ($item.PSIsContainer -or
    ($item.Extension -notin '.exe', '.dll' -and -not $nsisUninstaller)) {
    throw "Only an EXE, DLL, or NSIS-generated temporary uninstaller can be signed: $FilePath"
}

$arguments = @('sign', '/fd', 'SHA256')
$expectedThumbprint = $null
switch ($Mode) {
    'LocalTest' {
        $certificate = Get-LocalTestCertificate
        $expectedThumbprint = $certificate.Thumbprint
        $arguments += @('/s', 'My', '/sha1', $expectedThumbprint)
        # The local test signature deliberately has no public timestamp or trust.
    }
    'Store' {
        $Thumbprint = ($Thumbprint -replace '\s', '').ToUpperInvariant()
        if ($Thumbprint -notmatch '^[0-9A-F]{40}$') {
            throw 'Store mode requires CORAL_SIGN_THUMBPRINT (40 hex digits).'
        }
        $certificate = Get-MyCertificates 'CurrentUser' |
            Where-Object { $_.Thumbprint -eq $Thumbprint } | Select-Object -First 1
        if (-not $certificate) {
            $certificate = Get-MyCertificates 'LocalMachine' |
                Where-Object { $_.Thumbprint -eq $Thumbprint } | Select-Object -First 1
            if ($certificate) { $arguments += '/sm' }
        }
        if (-not $certificate -or -not $certificate.HasPrivateKey) {
            throw 'The production signing certificate/private key is unavailable in the My store.'
        }
        $expectedThumbprint = $Thumbprint
        $arguments += @('/s', 'My', '/sha1', $Thumbprint)
        if (-not $TimestampUrl) { $TimestampUrl = 'http://timestamp.digicert.com' }
    }
    'Artifact' {
        if (-not $ArtifactDlib -or -not (Test-Path -LiteralPath $ArtifactDlib)) {
            throw 'Artifact mode requires CORAL_SIGN_DLIB.'
        }
        if (-not $ArtifactMetadata -or -not (Test-Path -LiteralPath $ArtifactMetadata)) {
            throw 'Artifact mode requires CORAL_SIGN_METADATA.'
        }
        $arguments += @('/dlib', $ArtifactDlib, '/dmdf', $ArtifactMetadata)
        if (-not $TimestampUrl) { $TimestampUrl = 'http://timestamp.acs.microsoft.com' }
    }
}

if ($TimestampUrl) { $arguments += @('/tr', $TimestampUrl, '/td', 'SHA256') }

$file = $item.FullName
Write-Host "Signing [$Mode] $file"
& $signTool @arguments $file
if ($LASTEXITCODE -ne 0) { throw "SignTool failed for $file (exit $LASTEXITCODE)." }

if ($Mode -ne 'LocalTest') {
    & $signTool verify /pa /v $file
    if ($LASTEXITCODE -ne 0) {
        throw "Production Authenticode verification failed for $file (exit $LASTEXITCODE)."
    }
}
Write-Host "Signed [$Mode] $file"
