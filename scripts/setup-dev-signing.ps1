# Create a self-signed code-signing certificate for local development,
# trust it for the current user, and sign the current build.
# Re-run after reinstalling Windows or deleting the certificate.

$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'app\build-nmake\EarLabEngine_artefacts\Release\EarLab Engine.exe'
$subject = 'CN=EarLab Dev, O=EarLab'

$cert = Get-ChildItem Cert:\CurrentUser\My |
    Where-Object { $_.Subject -eq $subject -and $_.HasPrivateKey } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if (-not $cert) {
    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $subject `
        -KeyUsage DigitalSignature `
        -FriendlyName 'EarLab Dev Code Signing' `
        -CertStoreLocation Cert:\CurrentUser\My `
        -NotAfter (Get-Date).AddYears(5)

    Write-Host "created certificate $($cert.Thumbprint)"
} else {
    Write-Host "reusing certificate $($cert.Thumbprint)"
}

$cerPath = Join-Path $env:TEMP 'earlab-dev.cer'
Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null

foreach ($store in @('Root', 'TrustedPublisher')) {
    $already = Get-ChildItem "Cert:\CurrentUser\$store" |
        Where-Object { $_.Thumbprint -eq $cert.Thumbprint }

    if (-not $already) {
        & certutil.exe -user -addstore -f $store $cerPath | Out-Null
        Write-Host "installed into CurrentUser\$store"
    }
}

Remove-Item $cerPath -Force -ErrorAction SilentlyContinue

if (Test-Path $exe) {
    & (Join-Path $PSScriptRoot 'sign-app.ps1')
}

Write-Host ''
Write-Host 'Done. To remove later:'
Write-Host ('  Get-ChildItem Cert:\CurrentUser\My, Cert:\CurrentUser\Root, Cert:\CurrentUser\TrustedPublisher |')
Write-Host ('    Where-Object Thumbprint -eq ' + $cert.Thumbprint + ' | Remove-Item')
