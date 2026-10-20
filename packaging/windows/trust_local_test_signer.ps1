param([switch]$Remove)

$ErrorActionPreference = 'Stop'
$subject = 'CN=Coral Pinyin Local Test Code Signing'
$certificate = Get-ChildItem Cert:\CurrentUser\My |
    Where-Object { $_.Subject -eq $subject -and $_.HasPrivateKey } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1
if (-not $certificate) {
    throw 'No Coral Pinyin local test signing certificate exists in CurrentUser\My.'
}

# Only the public certificate is copied. The non-exportable private key stays in My.
$publicCertificate = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
    $certificate.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert))

foreach ($storeName in @('Root', 'TrustedPublisher')) {
    $store = [System.Security.Cryptography.X509Certificates.X509Store]::new(
        $storeName, [System.Security.Cryptography.X509Certificates.StoreLocation]::CurrentUser)
    try {
        $store.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
        $existing = $store.Certificates.Find(
            [System.Security.Cryptography.X509Certificates.X509FindType]::FindByThumbprint,
            $certificate.Thumbprint, $false)
        if ($Remove) {
            foreach ($item in $existing) { $store.Remove($item) }
            Write-Host "Removed local test trust from CurrentUser\$storeName"
        }
        elseif ($existing.Count -eq 0) {
            $store.Add($publicCertificate)
            Write-Host "Trusted local test signer in CurrentUser\$storeName"
        }
    }
    finally { $store.Close() }
}
Write-Host "Certificate thumbprint: $($certificate.Thumbprint)"
