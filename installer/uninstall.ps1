# EarLab per-user uninstaller (route C)
#
# Normally invoked by Windows "Apps & Features" through the HKCU
# UninstallString written by install.ps1. Can also be run directly:
#   powershell -NoProfile -ExecutionPolicy Bypass -File uninstall.ps1 [-Quiet] [-PurgeData]
#
# Removes: program files, Start Menu/desktop shortcuts, HKCU Apps entry.
# Keeps:   %APPDATA%\EarLab (training data) unless -PurgeData is given.

[CmdletBinding()]
param(
    [switch]$Quiet,
    [switch]$PurgeData
)

$ErrorActionPreference = 'Stop'

$appDir = Join-Path $env:LOCALAPPDATA 'Programs\EarLab'
$startMenuDir = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\EarLab'
$desktopLnk = Join-Path ([Environment]::GetFolderPath('Desktop')) 'EarLab.lnk'
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\EarLab'
$dataDir = Join-Path $env:APPDATA 'EarLab'

$running = @(Get-Process -Name 'EarLab Engine' -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    if ($Quiet) {
        $running | Stop-Process -Force
        Start-Sleep -Milliseconds 500
    } else {
        throw 'EarLab is running. Close it and run the uninstaller again.'
    }
}

# Remove shortcuts and the Apps & Features entry first, so a partially
# failed file deletion cannot leave a broken uninstall entry behind.
if (Test-Path -LiteralPath $startMenuDir) {
    Remove-Item -LiteralPath $startMenuDir -Recurse -Force
}
if (Test-Path -LiteralPath $desktopLnk) {
    # Only remove a direct-exe shortcut created by the installer, never a
    # pre-existing developer cmd.exe shortcut.
    try {
        $shell = New-Object -ComObject WScript.Shell
        $existing = $shell.CreateShortcut($desktopLnk)
        if ($existing.TargetPath -match 'EarLab Engine\.exe$') {
            Remove-Item -LiteralPath $desktopLnk -Force
        }
    } catch { }
}
if (Test-Path -LiteralPath $uninstallKey) {
    Remove-Item -LiteralPath $uninstallKey -Recurse -Force
}

if ($PurgeData) {
    if (-not $Quiet) {
        $answer = Read-Host 'Delete %APPDATA%\EarLab user data as well? [y/N]'
        if ($answer -notmatch '^[Yy]') { $PurgeData = $false }
    }
    if ($PurgeData -and (Test-Path -LiteralPath $dataDir)) {
        Remove-Item -LiteralPath $dataDir -Recurse -Force
    }
}

if (Test-Path -LiteralPath $appDir) {
    # uninstall.ps1 may live inside $appDir; PowerShell has already parsed it,
    # so the directory (including this script) can be deleted here.
    Remove-Item -LiteralPath $appDir -Recurse -Force
}

if (-not $Quiet) {
    Write-Host 'EarLab uninstalled.'
    if (Test-Path -LiteralPath $dataDir) {
        Write-Host "User data kept at $dataDir"
    }
}
