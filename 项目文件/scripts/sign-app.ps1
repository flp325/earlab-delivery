# Sign the built executable with the local EarLab dev certificate.
# Does nothing (exit 0) when no certificate or signtool is available,
# so incremental builds still work on machines without signing set up.

param([string]$Exe)

$root = Split-Path $PSScriptRoot -Parent

if (-not $Exe) {
    $Exe = Join-Path $root 'app\build-nmake\EarLabEngine_artefacts\Release\EarLab Engine.exe'
}

if (-not (Test-Path $Exe)) {
    Write-Host 'sign-app: executable not found, skipped'
    exit 0
}

$cert = Get-ChildItem Cert:\CurrentUser\My |
    Where-Object { $_.Subject -eq 'CN=EarLab Dev, O=EarLab' -and $_.HasPrivateKey } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if (-not $cert) {
    Write-Host 'sign-app: no dev certificate (run scripts/setup-dev-signing.ps1)'
    exit 0
}

$signtool = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter 'signtool.exe' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -like '*\x64\signtool.exe' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1

if (-not $signtool) {
    Write-Host 'sign-app: signtool.exe not found, skipped'
    exit 0
}

# Prefer an RFC3161 timestamp so the signature stays verifiable after the
# certificate expires; fall back to an untimestamped signature when the TSA
# is unreachable (offline builds must still be green).
$tsa = 'http://timestamp.digicert.com'
& $signtool.FullName sign /sha1 $cert.Thumbprint /fd SHA256 /tr $tsa /td SHA256 /d 'EarLab' $Exe | Out-Null

if ($LASTEXITCODE -ne 0) {
    Write-Host 'sign-app: RFC3161 timestamp failed, signing without timestamp'
    & $signtool.FullName sign /sha1 $cert.Thumbprint /fd SHA256 /d 'EarLab' $Exe | Out-Null
}

$sig = Get-AuthenticodeSignature -FilePath $Exe
if ($sig.Status -ne 'Valid') {
    throw "sign-app: signing failed ($($sig.Status)): $Exe"
}
$stamped = if ($null -ne $sig.TimeStamperCertificate) { 'timestamped' } else { 'no timestamp' }
Write-Host "sign-app: $($sig.Status) ($stamped)"
