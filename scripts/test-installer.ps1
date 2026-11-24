# EarLab A7 installer self-test (route C: portable ZIP + install.ps1)
#
# Runs the acceptance matrix against the shipped artifacts:
#   - setup.exe metadata/signature checks (route A artifact; not executed here:
#     this machine's Huorong HIPS denies writes from the locally signed setup)
#   - portable ZIP contents, including install.ps1 / uninstall.ps1 / launcher
#   - per-user install via install.ps1 -Quiet (no elevation, no UAC)
#   - layout, versions, app-local VC++ runtime, start menu, HKCU Apps entry
#   - installed exe launches for N seconds (screenshot) + --check-trims
#   - silent uninstall via uninstall.ps1 -Quiet, residue checks,
#     %APPDATA%\EarLab preservation (test sentinel)
#   - SHA256SUMS verification
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\scripts\test-installer.ps1
#
# SAFETY: refuses to run if %APPDATA%\EarLab already exists. It creates a
# sentinel there, proves uninstall keeps it, then removes the test-created
# folder so the machine is left as found.

param(
    [string]$SetupPath,
    [string]$PortableZip,
    [string]$Version = '0.6.1',
    [string]$ArtifactDir,
    [int]$LaunchSeconds = 8
)

$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent
if (-not $ArtifactDir) { $ArtifactDir = Join-Path $root 'artifacts' }

$dist = Join-Path $root 'dist'
if (-not $SetupPath)   { $SetupPath   = Join-Path $dist "EarLab-$Version-win64-setup.exe" }
if (-not $PortableZip) { $PortableZip = Join-Path $dist "EarLab-$Version-win64-portable.zip" }

New-Item -ItemType Directory -Force -Path $ArtifactDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logPath = Join-Path $ArtifactDir "installer-test-$stamp.log"
Start-Transcript -Path $logPath -Force | Out-Null

$script:checks = @()
$script:failures = @()
$script:exitCode = 0
$script:dataCreatedByTest = $false
$script:launchedPid = 0
$script:tempRoot = $null
$script:extractDir = $null

function Check {
    param([string]$Name, [bool]$Ok, [string]$Detail = '')
    $script:checks += [pscustomobject]@{ Name = $Name; Ok = $Ok; Detail = $Detail }
    if ($Ok) {
        Write-Host ("[PASS] {0}  {1}" -f $Name, $Detail) -ForegroundColor Green
    } else {
        Write-Host ("[FAIL] {0}  {1}" -f $Name, $Detail) -ForegroundColor Red
        $script:failures += $Name
    }
}

function Info {
    param([string]$Message)
    Write-Host ("[INFO] {0}" -f $Message) -ForegroundColor Cyan
}

# Native stderr + ErrorActionPreference=Stop is terminating in PowerShell 5.1.
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

function Format-Arg {
    param([string]$Value)
    if ($Value -match '[\s"]') { return '"' + $Value.Replace('"', '\"') + '"' }
    return $Value
}

# Direct CreateProcess: bypasses ShellExecute (no MOTW "open file" prompt) and
# returns a real exit code and captured stdout/stderr for GUI and console exes.
function Invoke-Exe {
    param(
        [string]$FilePath,
        [string[]]$Arguments = @(),
        [int]$TimeoutMs = 120000
    )
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FilePath
    $psi.Arguments = (@($Arguments | ForEach-Object { Format-Arg $_ }) -join ' ')
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $p = New-Object System.Diagnostics.Process
    $p.StartInfo = $psi
    [void]$p.Start()
    $stdoutTask = $p.StandardOutput.ReadToEndAsync()
    $stderrTask = $p.StandardError.ReadToEndAsync()
    $exited = $p.WaitForExit($TimeoutMs)
    if (-not $exited) {
        try { $p.Kill() } catch { }
        [void]$p.WaitForExit(5000)
    }
    $out = ''
    $err = ''
    try { $out = $stdoutTask.Result } catch { }
    try { $err = $stderrTask.Result } catch { }
    $code = $null
    if ($exited) { $code = $p.ExitCode }
    return [pscustomobject]@{ ExitCode = $code; Output = (($out + "`n" + $err).Trim()); TimedOut = (-not $exited) }
}

function Get-UninstallEntries {
    $roots = @(
        'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
        'HKCU:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall'
    )
    $found = @()
    foreach ($r in $roots) {
        if (Test-Path $r) {
            foreach ($child in Get-ChildItem $r -ErrorAction SilentlyContinue) {
                $props = Get-ItemProperty $child.PSPath -ErrorAction SilentlyContinue
                if ($props -and $props.DisplayName -like 'EarLab*') { $found += $props }
            }
        }
    }
    return $found
}

$appDir       = Join-Path $env:LOCALAPPDATA 'Programs\EarLab'
$installedExe = Join-Path $appDir 'EarLab Engine.exe'
$startMenuDir = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\EarLab'
$startLnk     = Join-Path $startMenuDir 'EarLab.lnk'
$dataDir      = Join-Path $env:APPDATA 'EarLab'
$desktopLnk   = Join-Path ([Environment]::GetFolderPath('Desktop')) 'EarLab.lnk'
$shotPath     = Join-Path $ArtifactDir 'a7-installed-8s.png'

try {
    # ---------------------------------------------------------------- setup artifact (route A)
    Check 'setup-artifact-exists' (Test-Path $SetupPath) $SetupPath
    if (Test-Path $SetupPath) {
        $setupInfo = (Get-Item $SetupPath).VersionInfo
        $setupFileVersion = $setupInfo.FileVersion.Trim()
        $setupProductVersion = $setupInfo.ProductVersion.Trim()
        Info ("setup FileVersion={0} ProductVersion={1}" -f $setupFileVersion, $setupProductVersion)
        Check 'setup-file-version' ($setupFileVersion -eq "$Version.0" -or $setupFileVersion -eq $Version) $setupFileVersion
        Check 'setup-product-version' ($setupProductVersion -eq $Version) $setupProductVersion
        $setupSig = Get-AuthenticodeSignature -FilePath $SetupPath
        Check 'setup-signature-valid' ($setupSig.Status -eq 'Valid') ("Status=" + $setupSig.Status + " Signer=" + $setupSig.SignerCertificate.Subject)
        $setupHasTimestamp = ($null -ne $setupSig.TimeStamperCertificate)
        Info ("setup timestamp: " + $(if ($setupHasTimestamp) { 'present (' + $setupSig.TimeStamperCertificate.Subject + ')' } else { 'ABSENT (TSA unreachable)' }))
        Info 'route A setup is NOT executed here: Huorong HIPS denies writes from the locally signed setup (Error 5); route C is the tested installer.'
    }

    # ---------------------------------------------------------------- preconditions
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    Check 'session-not-elevated' (-not $isAdmin) ("IsAdmin=" + $isAdmin)
    if (Test-Path $appDir) { throw "refusing to run: EarLab is already installed at $appDir" }
    if (Test-Path $dataDir) { throw "refusing to run: real user data exists at $dataDir (test must not touch it)" }
    Info 'pre-state: no install, no %APPDATA%\EarLab'
    $desktopExistedBefore = Test-Path $desktopLnk

    # ---------------------------------------------------------------- portable zip / route C payload
    Check 'portable-zip-exists' (Test-Path $PortableZip) $PortableZip
    if (-not (Test-Path $PortableZip)) { throw "portable zip not found: $PortableZip" }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($PortableZip)
    try { $entryNames = @($zip.Entries | ForEach-Object { $_.FullName }) } finally { $zip.Dispose() }
    $baseNames = @($entryNames | ForEach-Object { [System.IO.Path]::GetFileName($_) })
    foreach ($need in @('EarLab Engine.exe', 'msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll',
                        'EarLab-portable.cmd', 'install.ps1', 'uninstall.ps1', 'Install-EarLab.cmd',
                        'README.txt', 'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt', 'SOURCE-OFFER.txt')) {
        Check "portable-zip-$need" ($baseNames -contains $need) $need
    }

    $script:extractDir = Join-Path $env:TEMP ("earlab-a7-portable-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
    [System.IO.Compression.ZipFile]::ExtractToDirectory($PortableZip, $script:extractDir)
    $payloadExe = Join-Path $script:extractDir 'EarLab Engine.exe'
    $installScript = Join-Path $script:extractDir 'install.ps1'
    Check 'payload-exe-extracted' (Test-Path $payloadExe) $payloadExe
    Check 'payload-install-script' (Test-Path $installScript) $installScript
    if (Test-Path $payloadExe) {
        $portable = Invoke-Exe $payloadExe @('--check-trims') -TimeoutMs 120000
        Check 'portable-check-trims-PASS' ($portable.ExitCode -eq 0 -and $portable.Output -match 'PASS') ("exit=" + $portable.ExitCode + " out=" + $portable.Output)
    }
    if (-not (Test-Path $installScript)) { throw 'install.ps1 missing from zip' }

    # ---------------------------------------------------------------- route C install
    Info 'running install.ps1 -Quiet from the extracted portable ZIP...'
    $installResult = Invoke-Native 'powershell.exe' @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $installScript, '-Quiet')
    Check 'silent-install-exit0' ($installResult.ExitCode -eq 0) ("exit=" + $installResult.ExitCode)
    if ($installResult.ExitCode -ne 0) { throw "install.ps1 failed: $($installResult.Output -join ' ')" }
    $deadline = (Get-Date).AddSeconds(60)
    while (-not (Test-Path $installedExe) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
    Check 'installed-exe' (Test-Path $installedExe) $installedExe

    # ---------------------------------------------------------------- layout
    foreach ($dll in @('msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
        Check "installed-runtime-$dll" (Test-Path (Join-Path $appDir $dll)) $dll
    }
    foreach ($doc in @('README.txt', 'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt', 'SOURCE-OFFER.txt')) {
        Check "installed-doc-$doc" (Test-Path (Join-Path $appDir $doc)) $doc
    }
    Check 'installed-uninstaller-script' (Test-Path (Join-Path $appDir 'uninstall.ps1')) 'uninstall.ps1'
    if (Test-Path $installedExe) {
        $exeInfo = (Get-Item $installedExe).VersionInfo
        $exeVersion = $exeInfo.FileVersion.Trim()
        Check 'installed-exe-version' ($exeVersion -eq $Version -and $exeInfo.ProductVersion.Trim() -eq $Version) ("FileVersion=" + $exeVersion + " ProductVersion=" + $exeInfo.ProductVersion.Trim())
        $exeSig = Get-AuthenticodeSignature -FilePath $installedExe
        Check 'installed-exe-signature-valid' ($exeSig.Status -eq 'Valid') ("Status=" + $exeSig.Status)
        $trim = Invoke-Exe $installedExe @('--check-trims') -TimeoutMs 120000
        Check 'installed-check-trims-PASS' ($trim.ExitCode -eq 0 -and $trim.Output -match 'PASS') ("exit=" + $trim.ExitCode + " out=" + $trim.Output)
    }

    # ---------------------------------------------------------------- shortcuts / registry
    Check 'start-menu-lnk-exists' (Test-Path $startLnk) $startLnk
    if (Test-Path $startLnk) {
        $ws = New-Object -ComObject WScript.Shell
        $lnk = $ws.CreateShortcut($startLnk)
        Check 'start-menu-lnk-target' ($lnk.TargetPath -ieq $installedExe) ("TargetPath=" + $lnk.TargetPath)
        Check 'start-menu-lnk-not-cmd' ($lnk.TargetPath -notmatch 'cmd\.exe') 'direct exe shortcut'
        Check 'start-menu-lnk-workdir' ($lnk.WorkingDirectory -ieq $appDir) ("WorkingDirectory=" + $lnk.WorkingDirectory)
    }
    Check 'desktop-shortcut-unchanged' ((Test-Path $desktopLnk) -eq $desktopExistedBefore) ("present=" + (Test-Path $desktopLnk) + " existedBefore=" + $desktopExistedBefore)

    $entries = @(Get-UninstallEntries)
    Check 'hkcu-uninstall-entry-count' ($entries.Count -eq 1) ("count=" + $entries.Count)
    if ($entries.Count -ge 1) {
        $entry = $entries[0]
        Check 'hkcu-display-version' ($entry.DisplayVersion -eq $Version) ("DisplayVersion=" + $entry.DisplayVersion)
        Check 'hkcu-publisher' ($entry.Publisher -eq 'EarLab') ("Publisher=" + $entry.Publisher)
        Check 'hkcu-uninstall-string' ($entry.UninstallString -match 'uninstall\.ps1') ("UninstallString=" + $entry.UninstallString)
        Check 'hkcu-quiet-uninstall-string' ($entry.QuietUninstallString -match 'uninstall\.ps1.*-Quiet') ("QuietUninstallString=" + $entry.QuietUninstallString)
        Check 'hkcu-install-location' ($entry.InstallLocation -ieq $appDir) ("InstallLocation=" + $entry.InstallLocation)
        Info ("HKCU key: " + $entry.PSPath)
    }

    # ---------------------------------------------------------------- launch 8s + screenshot
    $script:tempRoot = Join-Path $env:TEMP ("earlab-a7-test-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Force -Path $script:tempRoot | Out-Null
    $tempProfile = Join-Path $script:tempRoot 'profile.json'

    Add-Type -AssemblyName System.Drawing
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class WinCapA7
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
    [WinCapA7]::SetProcessDPIAware() | Out-Null

    Info "launching installed exe with temp profile for $LaunchSeconds s..."
    $appPsi = New-Object System.Diagnostics.ProcessStartInfo
    $appPsi.FileName = $installedExe
    $appPsi.Arguments = (@('--page=assessment', "--profile=$tempProfile") | ForEach-Object { Format-Arg $_ }) -join ' '
    $appPsi.UseShellExecute = $false
    $app = New-Object System.Diagnostics.Process
    $app.StartInfo = $appPsi
    [void]$app.Start()
    $script:launchedPid = $app.Id
    Start-Sleep -Seconds $LaunchSeconds
    $app.Refresh()
    Check 'installed-app-alive-after-launch' (-not $app.HasExited) ("pid=" + $app.Id + " alive=" + (-not $app.HasExited))

    $shotOk = $false
    $h = $app.MainWindowHandle
    if ($h -eq [IntPtr]::Zero) {
        Start-Sleep -Seconds 2
        $app.Refresh()
        $h = $app.MainWindowHandle
    }
    if ($h -ne [IntPtr]::Zero) {
        [WinCapA7]::SetForegroundWindow($h) | Out-Null
        Start-Sleep -Milliseconds 600
        $rect = New-Object -TypeName 'WinCapA7+RECT'
        [WinCapA7]::GetWindowRect($h, [ref]$rect) | Out-Null
        $w = $rect.Right - $rect.Left
        $hh = $rect.Bottom - $rect.Top
        if ($w -gt 0 -and $hh -gt 0) {
            $bmp = New-Object System.Drawing.Bitmap $w, $hh
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $hdc = $g.GetHdc()
            [WinCapA7]::PrintWindow($h, $hdc, 2) | Out-Null
            $g.ReleaseHdc($hdc)
            $g.Dispose()
            $bmp.Save($shotPath, [System.Drawing.Imaging.ImageFormat]::Png)
            $bmp.Dispose()
            $shotOk = Test-Path $shotPath
        }
    }
    if (-not $shotOk) {
        $screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        $bmp = New-Object System.Drawing.Bitmap $screen.Width, $screen.Height
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($screen.X, $screen.Y, 0, 0, $bmp.Size)
        $g.Dispose()
        $bmp.Save($shotPath, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        $shotOk = Test-Path $shotPath
    }
    Check 'installed-app-screenshot' ($shotOk -and ((Get-Item $shotPath).Length -gt 5000)) ("path=" + $shotPath + " bytes=" + $(if (Test-Path $shotPath) { (Get-Item $shotPath).Length } else { 0 }))

    Info "taskkill pid $($app.Id)..."
    Invoke-Native 'taskkill.exe' @('/PID', [string]$app.Id, '/T', '/F') -Quiet | Out-Null
    $killDeadline = (Get-Date).AddSeconds(10)
    while (-not $app.HasExited -and (Get-Date) -lt $killDeadline) { Start-Sleep -Milliseconds 300; $app.Refresh() }
    Check 'installed-app-killed' ($app.HasExited) ("pid=" + $app.Id)

    # ---------------------------------------------------------------- data preservation
    $script:dataCreatedByTest = $true
    New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
    $sentinel = Join-Path $dataDir 'a7-uninstall-preserve.txt'
    $sentinelValue = "a7-keep-" + [guid]::NewGuid().ToString('N')
    Set-Content -Path $sentinel -Value $sentinelValue -Encoding ASCII
    Check 'data-sentinel-created' ((Test-Path $sentinel) -and ((Get-Content $sentinel -Raw).Trim() -eq $sentinelValue)) $sentinel

    # ---------------------------------------------------------------- uninstall
    $installedUninstall = Join-Path $appDir 'uninstall.ps1'
    Check 'uninstaller-script-exists' (Test-Path $installedUninstall) $installedUninstall
    if (Test-Path $installedUninstall) {
        Info 'running uninstall.ps1 -Quiet...'
        $un = Invoke-Native 'powershell.exe' @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $installedUninstall, '-Quiet')
        Check 'silent-uninstall-exit0' ($un.ExitCode -eq 0) ("exit=" + $un.ExitCode)
        $uniDeadline = (Get-Date).AddSeconds(60)
        while ((Test-Path $appDir) -and (Get-Date) -lt $uniDeadline) { Start-Sleep -Milliseconds 500 }
    }

    Check 'install-dir-removed' (-not (Test-Path $appDir)) $appDir
    Check 'start-menu-removed' (-not (Test-Path $startMenuDir)) $startMenuDir
    $entriesAfter = @(Get-UninstallEntries)
    Check 'hkcu-uninstall-removed' ($entriesAfter.Count -eq 0) ("count=" + $entriesAfter.Count)
    Check 'user-data-preserved' ((Test-Path $sentinel) -and ((Get-Content $sentinel -Raw).Trim() -eq $sentinelValue)) 'sentinel still present after uninstall'
    Check 'desktop-shortcut-not-left-behind' ((Test-Path $desktopLnk) -eq $desktopExistedBefore) ("present=" + (Test-Path $desktopLnk))

    # ---------------------------------------------------------------- hashes
    $sumsPath = Join-Path $dist 'SHA256SUMS.txt'
    Check 'sha256sums-exists' (Test-Path $sumsPath) $sumsPath
    if (Test-Path $sumsPath) {
        $bad = @()
        $sumLines = @(Get-Content $sumsPath | Where-Object { $_ -match '\S' })
        foreach ($line in $sumLines) {
            if ($line -match '^([0-9A-Fa-f]{64})\s+\*?(.+)$') {
                $expected = $Matches[1].ToUpperInvariant()
                $name = $Matches[2].Trim()
                $file = Join-Path $dist $name
                if (-not (Test-Path $file)) { $bad += "$name (missing)"; continue }
                $actual = (Get-FileHash $file -Algorithm SHA256).Hash
                if ($actual -ne $expected) { $bad += "$name (mismatch)" }
            } else {
                $bad += "unparsable line: $line"
            }
        }
        Check 'sha256sums-verify' ($bad.Count -eq 0 -and $sumLines.Count -ge 2) ("entries=" + $sumLines.Count + " bad=" + ($bad -join '; '))
        Check 'sha256sums-lists-setup' (($sumLines -join "`n") -match 'EarLab-.*-win64-setup\.exe') 'setup hashed'
        Check 'sha256sums-lists-portable' (($sumLines -join "`n") -match 'EarLab-.*-win64-portable\.zip') 'portable hashed'
    }
}
catch {
    Check 'test-harness-completed' $false $_.Exception.Message
    $script:exitCode = 1
}
finally {
    # ---- cleanup: never leave an install or test data behind
    if ($script:launchedPid -ne 0) {
        Invoke-Native 'taskkill.exe' @('/PID', [string]$script:launchedPid, '/T', '/F') -Quiet | Out-Null
    }

    if (Test-Path $appDir) {
        $unins = Join-Path $appDir 'uninstall.ps1'
        if (Test-Path $unins) {
            Info 'cleanup: uninstalling leftover test install...'
            Invoke-Native 'powershell.exe' @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $unins, '-Quiet') -Quiet | Out-Null
            $cleanDeadline = (Get-Date).AddSeconds(60)
            while ((Test-Path $appDir) -and (Get-Date) -lt $cleanDeadline) { Start-Sleep -Milliseconds 500 }
        }
        if (Test-Path $appDir) { Remove-Item $appDir -Recurse -Force -ErrorAction SilentlyContinue }
        if (Test-Path $startMenuDir) { Remove-Item $startMenuDir -Recurse -Force -ErrorAction SilentlyContinue }
        foreach ($entry in @(Get-UninstallEntries)) {
            if ($entry.PSPath) { Remove-Item $entry.PSPath -Recurse -Force -ErrorAction SilentlyContinue }
        }
    }

    if ($script:dataCreatedByTest -and (Test-Path $dataDir)) {
        Info 'cleanup: restoring %APPDATA%\EarLab to its pre-test (absent) state'
        Remove-Item $dataDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    if ($script:tempRoot -and (Test-Path $script:tempRoot)) { Remove-Item $script:tempRoot -Recurse -Force -ErrorAction SilentlyContinue }
    if ($script:extractDir -and (Test-Path $script:extractDir)) { Remove-Item $script:extractDir -Recurse -Force -ErrorAction SilentlyContinue }

    Write-Host ''
    Write-Host '================ A7 INSTALLER SELF-TEST SUMMARY ================'
    $passed = @($script:checks | Where-Object Ok).Count
    $failed = @($script:checks | Where-Object { -not $_.Ok }).Count
    Write-Host ("checks: {0} passed, {1} failed, {2} total" -f $passed, $failed, $script:checks.Count)
    if ($failed -gt 0) {
        Write-Host 'failures:'
        $script:checks | Where-Object { -not $_.Ok } | ForEach-Object { Write-Host ("  - " + $_.Name + "  " + $_.Detail) }
        $script:exitCode = 1
    } else {
        Write-Host 'RESULT: PASS' -ForegroundColor Green
    }
    Write-Host ("log: " + $logPath)
    Write-Host '==============================================================='

    try { Stop-Transcript | Out-Null } catch { }
}

exit $script:exitCode
