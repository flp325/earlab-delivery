# EarLab A7 installer build (Inno Setup 6 per-user + portable ZIP)
#
# One command:
#   powershell -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1
#
# What it does (idempotent):
#   1. validates the built exe (version, signature) and the VC++ 2022 x64
#      runtime source (four app-local DLLs);
#   2. stages app files + licenses into <dist>\stage;
#   3. compiles the per-user Inno Setup installer (PrivilegesRequired=lowest,
#      installs to %LOCALAPPDATA%\Programs\EarLab);
#   4. signs the setup with the local dev certificate, attempting an RFC3161
#      timestamp (falls back to an untimestamped signature if the TSA is
#      unreachable, and records which happened);
#   5. builds the portable ZIP from the same stage, with a portable launcher;
#   6. writes BUILD-INFO.txt and SHA256SUMS.txt.
#
# Logs go to artifacts\build-installer-<timestamp>.log.
# Exits non-zero on any failure. Signing is fail-closed: no valid signature
# means no successful build (review P1-3). Output defaults to <repo>\dist.
#
# Parameters:
#   -Version 0.6.1          must match app\CMakeLists.txt and the exe resource
#   -OutputDir <path>       default <repo>\dist (relative paths resolved from repo root)
#   -StageDir <path>        default <OutputDir>\stage
#   -ExePath <path>         default app\build-nmake\...\Release\EarLab Engine.exe
#   -IsccPath <path>        default: auto-detect per-user Inno Setup 6
#   -SkipPortable           skip the portable ZIP (not recommended for release)

[CmdletBinding()]
param(
    [string]$Version = '0.6.1',
    [string]$OutputDir = '',
    [string]$StageDir = '',
    [string]$ExePath = '',
    [string]$IsccPath = '',
    [switch]$SkipPortable
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent
$installerDir = Join-Path $root 'installer'
$issPath = Join-Path $installerDir 'earlab.iss'
$artifactsDir = Join-Path $root 'artifacts'

function Resolve-RepoPath {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) { return [System.IO.Path]::GetFullPath($Path) }
    return [System.IO.Path]::GetFullPath((Join-Path $root $Path))
}

# Windows PowerShell 5.1 turns native stderr into a terminating error when
# ErrorActionPreference=Stop. That would make the timestamp fallback (and any
# diagnostic output) impossible, so run native tools with Continue and check
# the exit code explicitly.
function Invoke-Native {
    param(
        [string]$FilePath,
        [string[]]$Arguments = @(),
        [switch]$Quiet
    )
    $oldEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = @(& $FilePath @Arguments 2>&1)
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldEap
    }
    if (-not $Quiet) {
        foreach ($line in $out) { Write-Host ("    " + $line) }
    }
    return [pscustomobject]@{ ExitCode = $code; Output = $out }
}

function Assert-LastExitCode {
    param([string]$What)
    if ($LASTEXITCODE -ne 0) { throw "$What failed with exit code $LASTEXITCODE" }
}

function Get-FileSha256 {
    param([string]$Path)
    return (Get-FileHash -Path $Path -Algorithm SHA256).Hash
}

function Find-Signtool {
    $candidates = @()
    $kitsRoot = 'C:\Program Files (x86)\Windows Kits\10\bin'
    if (Test-Path $kitsRoot) {
        $candidates = @(Get-ChildItem $kitsRoot -Recurse -Filter 'signtool.exe' -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -like '*\x64\signtool.exe' } |
            Sort-Object FullName -Descending)
    }
    if ($candidates.Count -gt 0) { return $candidates[0].FullName }
    return $null
}

function Find-Iscc {
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return (Resolve-Path $c).Path }
    }
    $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $perUser = Join-Path $env:LOCALAPPDATA 'Programs'
    if (Test-Path $perUser) {
        $found = Get-ChildItem $perUser -Recurse -Filter 'ISCC.exe' -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    return $null
}

function Find-VcRuntimeDir {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsPath = $null
    if ((Test-Path $vswhere) -and (Get-Command $vswhere)) {
        $r = Invoke-Native $vswhere @('-latest', '-products', '*',
            '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
            '-property', 'installationPath') -Quiet
        if ($r.ExitCode -ne 0) { throw "vswhere failed with exit code $($r.ExitCode)" }
        $vsPath = @($r.Output | Where-Object { $_ -and $_.ToString().Trim() })[0]
        if ($vsPath) { $vsPath = $vsPath.ToString().Trim() }
    }

    $roots = @()
    if ($vsPath) { $roots += (Join-Path $vsPath 'VC\Redist\MSVC') }
    $roots += (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC')
    $roots += (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\Community\VC\Redist\MSVC')

    foreach ($r in $roots) {
        if (-not (Test-Path $r)) { continue }
        $versions = @(Get-ChildItem $r -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like '14.*' } | Sort-Object Name -Descending)
        foreach ($v in $versions) {
            $crt = Join-Path $v.FullName 'x64\Microsoft.VC143.CRT'
            if (Test-Path $crt) {
                return [pscustomobject]@{ Path = $crt; Version = $v.Name; RedistRoot = $r }
            }
        }
    }
    return $null
}

# ---------------------------------------------------------------- log setup
New-Item -ItemType Directory -Force -Path $artifactsDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logPath = Join-Path $artifactsDir "build-installer-$stamp.log"
Start-Transcript -Path $logPath -Force | Out-Null

$script:exitCode = 0
try {
    Write-Host "EarLab installer build: version=$Version"
    Write-Host "repo root: $root"

    # ------------------------------------------------------------ paths
    if (-not $OutputDir) { $OutputDir = Join-Path $root 'dist' }
    $out = Resolve-RepoPath $OutputDir
    if (-not $StageDir) { $StageDir = Join-Path $out 'stage' }
    $stage = Resolve-RepoPath $StageDir
    if (-not $ExePath) { $ExePath = Join-Path $root 'app\build-nmake\EarLabEngine_artefacts\Release\EarLab Engine.exe' }
    $exe = Resolve-RepoPath $ExePath
    $setup = Join-Path $out "EarLab-$Version-win64-setup.exe"
    $portableZip = Join-Path $out "EarLab-$Version-win64-portable.zip"
    $buildInfoPath = Join-Path $out 'BUILD-INFO.txt'
    $sumsPath = Join-Path $out 'SHA256SUMS.txt'
    $portableDir = Join-Path $out 'portable'

    foreach ($required in @($issPath,
                            (Join-Path $installerDir 'LICENSE'),
                            (Join-Path $installerDir 'README.txt'),
                            (Join-Path $installerDir 'THIRD-PARTY-NOTICES.txt'),
                            (Join-Path $installerDir 'SOURCE-OFFER.txt'),
                            (Join-Path $installerDir 'portable-launcher.cmd'))) {
        if (-not (Test-Path $required)) { throw "missing installer asset: $required" }
    }

    # ------------------------------------------------------------ exe validation
    if (-not (Test-Path $exe)) { throw "app exe not found: $exe (run scripts\build.ps1 first)" }
    $exeInfo = (Get-Item $exe).VersionInfo
    Write-Host ("exe: {0} ({1} bytes, FileVersion={2}, ProductVersion={3})" -f $exe, (Get-Item $exe).Length, $exeInfo.FileVersion, $exeInfo.ProductVersion)
    if ($exeInfo.FileVersion -ne $Version) { throw "exe FileVersion '$($exeInfo.FileVersion)' does not match requested version '$Version'" }
    if ($exeInfo.ProductVersion -ne $Version) { throw "exe ProductVersion '$($exeInfo.ProductVersion)' does not match requested version '$Version'" }

    $cmakeText = Get-Content (Join-Path $root 'app\CMakeLists.txt') -Raw
    if ($cmakeText -notmatch "project\(EarLab VERSION $([regex]::Escape($Version))") {
        throw "app\CMakeLists.txt project() version does not match '$Version'"
    }

    $exeSig = Get-AuthenticodeSignature -FilePath $exe
    if ($exeSig.Status -ne 'Valid') {
        throw "app exe signature is not valid (Status=$($exeSig.Status)). Run scripts\sign-app.ps1 or setup-dev-signing.ps1 first."
    }
    Write-Host ("exe signature: Valid, signer={0}, timestamp={1}" -f $exeSig.SignerCertificate.Subject, $(if ($null -ne $exeSig.TimeStamperCertificate) { 'yes' } else { 'no' }))

    # ------------------------------------------------------------ tool discovery
    if (-not $IsccPath) {
        $IsccPath = Find-Iscc
        if (-not $IsccPath) {
            throw 'ISCC.exe not found. Install Inno Setup 6 per-user: winget install --id JRSoftware.InnoSetup -e --scope user --silent --accept-package-agreements --accept-source-agreements (no admin required).'
        }
    }
    if (-not (Test-Path $IsccPath)) { throw "ISCC.exe not found at $IsccPath" }
    $isccInfo = (Get-Item $IsccPath).VersionInfo
    Write-Host ("ISCC: {0} (version {1})" -f $IsccPath, $isccInfo.ProductVersion)

    $signtool = Find-Signtool
    $cert = Get-ChildItem Cert:\CurrentUser\My |
        Where-Object { $_.Subject -eq 'CN=EarLab Dev, O=EarLab' -and $_.HasPrivateKey } |
        Sort-Object NotAfter -Descending | Select-Object -First 1
    if (-not $signtool) { throw 'signtool.exe not found (Windows SDK). Release builds are fail-closed on signing.' }
    if (-not $cert) { throw 'dev signing certificate not found in Cert:\CurrentUser\My (subject CN=EarLab Dev, O=EarLab). Run scripts\setup-dev-signing.ps1.' }
    Write-Host ("signtool: {0}" -f $signtool)
    Write-Host ("cert: {0}, thumbprint={1}, expires={2}" -f $cert.Subject, $cert.Thumbprint, $cert.NotAfter)

    $vc = Find-VcRuntimeDir
    if (-not $vc) { throw 'VC++ 2022 x64 redistributable directory (x64\Microsoft.VC143.CRT) not found.' }
    Write-Host ("VC++ runtime: {0} (version {1})" -f $vc.Path, $vc.Version)
    foreach ($dll in @('msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
        $src = Join-Path $vc.Path $dll
        if (-not (Test-Path $src)) { throw "missing VC++ runtime DLL: $src" }
    }

    # BOM check: Inno Setup displays LICENSE and README in the wizard; both
    # contain non-ASCII text and must be UTF-8 with BOM to render correctly.
    foreach ($displayFile in @((Join-Path $installerDir 'LICENSE'), (Join-Path $installerDir 'README.txt'))) {
        $bytes = [System.IO.File]::ReadAllBytes($displayFile)
        if ($bytes.Length -lt 3 -or $bytes[0] -ne 0xEF -or $bytes[1] -ne 0xBB -or $bytes[2] -ne 0xBF) {
            throw "installer text file must be UTF-8 with BOM: $displayFile"
        }
    }

    # ------------------------------------------------------------ stage (idempotent)
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $stage | Out-Null

    Copy-Item $exe (Join-Path $stage 'EarLab Engine.exe') -Force
    foreach ($dll in @('msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
        Copy-Item (Join-Path $vc.Path $dll) $stage -Force
    }
    Copy-Item (Join-Path $installerDir 'LICENSE') (Join-Path $stage 'LICENSE.txt') -Force
    foreach ($doc in @('README.txt', 'THIRD-PARTY-NOTICES.txt', 'SOURCE-OFFER.txt')) {
        Copy-Item (Join-Path $installerDir $doc) $stage -Force
    }

    $stageFiles = @(Get-ChildItem $stage -File | Sort-Object Name)
    Write-Host 'stage contents:'
    $stageFiles | ForEach-Object { Write-Host ("  {0,-28} {1,10} bytes" -f $_.Name, $_.Length) }
    foreach ($need in @('EarLab Engine.exe', 'msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
                        'LICENSE.txt', 'README.txt', 'THIRD-PARTY-NOTICES.txt', 'SOURCE-OFFER.txt')) {
        if (-not (Test-Path (Join-Path $stage $need))) { throw "stage is missing $need" }
    }

    # ------------------------------------------------------------ build installer
    if (Test-Path $setup) { Remove-Item $setup -Force }
    Write-Host "compiling installer -> $setup"
    $isccResult = Invoke-Native $IsccPath @('/Qp', "/DMyAppVersion=$Version", "/DStageDir=$stage", "/DOutputDir=$out", $issPath)
    if ($isccResult.ExitCode -ne 0) { throw "ISCC failed with exit code $($isccResult.ExitCode)" }
    if (-not (Test-Path $setup)) { throw "ISCC reported success but $setup was not produced" }
    $setupLength = (Get-Item $setup).Length
    Write-Host ("setup: {0} ({1} bytes)" -f $setup, $setupLength)

    $setupVi = (Get-Item $setup).VersionInfo
    $setupFileVersion = $setupVi.FileVersion.Trim()
    $setupProductVersion = $setupVi.ProductVersion.Trim()
    Write-Host ("setup VersionInfo: FileVersion={0} ProductVersion={1} Description={2}" -f $setupFileVersion, $setupProductVersion, $setupVi.FileDescription.Trim())
    if ($setupFileVersion -ne "$Version.0" -and $setupFileVersion -ne $Version) { throw "setup FileVersion '$setupFileVersion' != '$Version.0'" }
    if ($setupProductVersion -ne $Version) { throw "setup ProductVersion '$setupProductVersion' != '$Version'" }

    # ------------------------------------------------------------ sign setup
    $timestampUrl = 'http://timestamp.digicert.com'
    Write-Host "signing setup (RFC3161: $timestampUrl)..."
    $signResult = Invoke-Native $signtool @('sign', '/sha1', $cert.Thumbprint, '/fd', 'SHA256',
        '/tr', $timestampUrl, '/td', 'SHA256', '/d', "EarLab $Version Setup", $setup)
    $timestamped = $false
    if ($signResult.ExitCode -ne 0) {
        Write-Warning "timestamped signing failed (exit $($signResult.ExitCode)); retrying without RFC3161 timestamp"
        $retry = Invoke-Native $signtool @('sign', '/sha1', $cert.Thumbprint, '/fd', 'SHA256',
            '/d', "EarLab $Version Setup", $setup)
        if ($retry.ExitCode -ne 0) { throw "signtool sign failed with exit code $($retry.ExitCode)" }
    }

    $setupSig = Get-AuthenticodeSignature -FilePath $setup
    if ($setupSig.Status -ne 'Valid') { throw "setup signature invalid after signing (Status=$($setupSig.Status))" }
    $timestamped = ($null -ne $setupSig.TimeStamperCertificate)
    Write-Host ("setup signature: Valid, signer={0}, timestamp={1}" -f $setupSig.SignerCertificate.Subject, $(if ($timestamped) { 'yes' } else { 'NO' }))

    # ------------------------------------------------------------ portable zip
    if (-not $SkipPortable) {
        if (Test-Path $portableDir) { Remove-Item $portableDir -Recurse -Force }
        New-Item -ItemType Directory -Force -Path $portableDir | Out-Null
        Copy-Item (Join-Path $stage '*') $portableDir -Recurse -Force
        Copy-Item (Join-Path $installerDir 'portable-launcher.cmd') (Join-Path $portableDir 'EarLab-portable.cmd') -Force
        # Route C installer scripts live next to the app payload so the ZIP
        # is both a portable build and a per-user installer package.
        foreach ($routeCFile in @('install.ps1', 'uninstall.ps1', 'Install-EarLab.cmd')) {
            Copy-Item (Join-Path $installerDir $routeCFile) (Join-Path $portableDir $routeCFile) -Force
        }

        if (Test-Path $portableZip) { Remove-Item $portableZip -Force }
        Write-Host "building portable zip -> $portableZip"
        Compress-Archive -Path (Join-Path $portableDir '*') -DestinationPath $portableZip -CompressionLevel Optimal -Force
        if (-not (Test-Path $portableZip)) { throw "portable zip not produced: $portableZip" }

        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [System.IO.Compression.ZipFile]::OpenRead($portableZip)
        try {
            $zipNames = @($zip.Entries | ForEach-Object { [System.IO.Path]::GetFileName($_.FullName) })
        } finally {
            $zip.Dispose()
        }
        foreach ($need in @('EarLab Engine.exe', 'msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
                            'LICENSE.txt', 'README.txt', 'THIRD-PARTY-NOTICES.txt', 'SOURCE-OFFER.txt',
                            'EarLab-portable.cmd', 'install.ps1', 'uninstall.ps1', 'Install-EarLab.cmd')) {
            if ($zipNames -notcontains $need) { throw "portable zip is missing $need" }
        }
        Write-Host ("portable zip: {0} ({1} bytes, {2} entries)" -f $portableZip, (Get-Item $portableZip).Length, $zipNames.Count)
        Remove-Item $portableDir -Recurse -Force
    }

    # ------------------------------------------------------------ build info + hashes
    $gitCommit = 'unknown'
    $gitDescribe = 'unknown'
    $gitTrackedDirty = 'unknown'
    $git = Get-Command git -ErrorAction SilentlyContinue
    if ($git) {
        $r = Invoke-Native $git.Source @('-C', $root, 'rev-parse', 'HEAD') -Quiet
        if ($r.ExitCode -eq 0) { $gitCommit = (@($r.Output)[0]).ToString().Trim() }
        $r = Invoke-Native $git.Source @('-C', $root, 'describe', '--tags', '--always', '--dirty') -Quiet
        if ($r.ExitCode -eq 0) { $gitDescribe = (@($r.Output)[0]).ToString().Trim() }
        $r = Invoke-Native $git.Source @('-C', $root, 'status', '--porcelain') -Quiet
        if ($r.ExitCode -eq 0) {
            $status = @($r.Output | Where-Object { $_ -and $_.ToString().Trim() })
            $gitTrackedDirty = if (@($status | Where-Object { $_ -notmatch '^\?\?' }).Count -gt 0) { 'yes' } else { 'no' }
        }
    }

    $signingNote = if ($timestamped) { "RFC3161 timestamp present ($timestampUrl)" } else { 'no RFC3161 timestamp (TSA unreachable); dev certificate only' }
    $buildInfo = @()
    $buildInfo += "EarLab $Version"
    $buildInfo += "Git commit: $gitCommit"
    $buildInfo += "Git describe: $gitDescribe"
    $buildInfo += "Git tracked changes: $gitTrackedDirty"
    $buildInfo += "Build date: $(Get-Date -Format o)"
    $buildInfo += "Installer: Inno Setup 6 (per-user, PrivilegesRequired=lowest)"
    $buildInfo += "Fallback installer: portable ZIP + install.ps1 (per-user, HKCU uninstall entry)"
    $buildInfo += "ISCC: $IsccPath"
    $buildInfo += "VC++ runtime source: $($vc.Path) (version $($vc.Version))"
    $buildInfo += "Signing cert: $($cert.Subject), thumbprint $($cert.Thumbprint)"
    $buildInfo += "Signing: $signingNote"
    $buildInfo += "Setup: $(Split-Path $setup -Leaf) SHA256 $(Get-FileSha256 $setup)"
    if (-not $SkipPortable) { $buildInfo += "Portable: $(Split-Path $portableZip -Leaf) SHA256 $(Get-FileSha256 $portableZip)" }
    $buildInfo | Set-Content -Path $buildInfoPath -Encoding UTF8
    Write-Host "build info -> $buildInfoPath"

    $hashTargets = @($setup)
    if (-not $SkipPortable) { $hashTargets += $portableZip }
    $hashTargets += $buildInfoPath
    $sumLines = foreach ($f in $hashTargets) {
        '{0}  {1}' -f (Get-FileSha256 $f), (Split-Path $f -Leaf)
    }
    $sumLines | Set-Content -Path $sumsPath -Encoding ASCII
    Write-Host "hashes -> $sumsPath"
    $sumLines | ForEach-Object { Write-Host "  $_" }

    # ------------------------------------------------------------ summary
    Write-Host ''
    Write-Host '================ BUILD SUMMARY ================'
    Write-Host ("version:      {0}" -f $Version)
    Write-Host ("setup:        {0}" -f $setup)
    Write-Host ("portable:     {0}" -f $(if ($SkipPortable) { '(skipped)' } else { $portableZip }))
    Write-Host ("sha256sums:   {0}" -f $sumsPath)
    Write-Host ("setup sha256: {0}" -f (Get-FileSha256 $setup))
    if (-not $SkipPortable) { Write-Host ("zip sha256:   {0}" -f (Get-FileSha256 $portableZip)) }
    Write-Host ("signature:    Valid (timestamp={0})" -f $(if ($timestamped) { 'yes' } else { 'no' }))
    Write-Host ("log:          {0}" -f $logPath)
    Write-Host '==============================================='
}
catch {
    Write-Host ''
    Write-Host ("BUILD FAILED: " + $_.Exception.Message) -ForegroundColor Red
    Write-Host ("log: " + $logPath)
    $script:exitCode = 1
}
finally {
    try { Stop-Transcript | Out-Null } catch { }
}

exit $script:exitCode
