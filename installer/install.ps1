# EarLab per-user installer (route C: portable ZIP + PowerShell)
#
# Run from an extracted EarLab-0.6.1-win64-portable.zip:
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\install.ps1
# or double-click Install-EarLab.cmd
#
# What it does:
#   - copies the app (exe + VC++ runtime DLLs + docs) to
#     %LOCALAPPDATA%\Programs\EarLab (no administrator, no UAC);
#   - creates a Start Menu shortcut that targets "EarLab Engine.exe" directly
#     (never cmd.exe); optional desktop shortcut with -DesktopShortcut;
#   - registers a per-user entry under
#     HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\EarLab
#     with DisplayName / DisplayVersion / Publisher / UninstallString;
#   - copies uninstall.ps1 into the install directory.
#
# User data in %APPDATA%\EarLab is never touched by install or uninstall.
# This script is idempotent: re-running it upgrades the files in place and
# refreshes the shortcut and uninstall entry.

[CmdletBinding()]
param(
    [switch]$Quiet,
    [switch]$DesktopShortcut,
    [string]$PayloadDir = ''
)

$ErrorActionPreference = 'Stop'

if (-not $PayloadDir) { $PayloadDir = $PSScriptRoot }
if (-not $PayloadDir) { throw 'PayloadDir is required when the script is not run from a file.' }
$source = (Resolve-Path -LiteralPath $PayloadDir).Path

$sourceExe = Join-Path $source 'EarLab Engine.exe'
$sourceUninstall = Join-Path $source 'uninstall.ps1'
if (-not (Test-Path -LiteralPath $sourceExe)) { throw "payload is missing 'EarLab Engine.exe': $source" }
if (-not (Test-Path -LiteralPath $sourceUninstall)) { throw "payload is missing uninstall.ps1: $source" }

$exeInfo = (Get-Item -LiteralPath $sourceExe).VersionInfo
$version = $exeInfo.FileVersion.Trim()
if (-not $version) { $version = $exeInfo.ProductVersion.Trim() }
if (-not $version) { throw 'cannot determine EarLab version from the payload exe' }

$appDir = Join-Path $env:LOCALAPPDATA 'Programs\EarLab'
$startMenuDir = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\EarLab'
$startLnk = Join-Path $startMenuDir 'EarLab.lnk'
$desktopLnk = Join-Path ([Environment]::GetFolderPath('Desktop')) 'EarLab.lnk'
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\EarLab'
$installedExe = Join-Path $appDir 'EarLab Engine.exe'
$installedUninstall = Join-Path $appDir 'uninstall.ps1'

$running = @(Get-Process -Name 'EarLab Engine' -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    if ($Quiet) {
        $running | Stop-Process -Force
        Start-Sleep -Milliseconds 500
    } else {
        throw 'EarLab is running. Close it and run the installer again.'
    }
}

if (-not $Quiet) { Write-Host "Installing EarLab $version to $appDir" }
New-Item -ItemType Directory -Force -Path $appDir | Out-Null

$sourceFull = [System.IO.Path]::GetFullPath($source)
$appFull = [System.IO.Path]::GetFullPath($appDir)
if (-not $sourceFull.Equals($appFull, [System.StringComparison]::OrdinalIgnoreCase)) {
    Copy-Item -Path (Join-Path $source '*') -Destination $appDir -Recurse -Force
}
Copy-Item -LiteralPath $sourceUninstall -Destination $installedUninstall -Force

# Start Menu shortcut: direct exe target, never cmd.exe.
New-Item -ItemType Directory -Force -Path $startMenuDir | Out-Null
$shell = New-Object -ComObject WScript.Shell
$lnk = $shell.CreateShortcut($startLnk)
$lnk.TargetPath = $installedExe
$lnk.WorkingDirectory = $appDir
$lnk.Description = "EarLab $version"
$lnk.IconLocation = "$installedExe,0"
$lnk.Save()

if ($DesktopShortcut) {
    $desktop = $shell.CreateShortcut($desktopLnk)
    $desktop.TargetPath = $installedExe
    $desktop.WorkingDirectory = $appDir
    $desktop.Description = "EarLab $version"
    $desktop.IconLocation = "$installedExe,0"
    $desktop.Save()
}

# Per-user Apps & Features entry (HKCU, no elevation).
New-Item -Path $uninstallKey -Force | Out-Null
$sizeBytes = (Get-ChildItem -LiteralPath $appDir -Recurse -File | Measure-Object -Property Length -Sum).Sum
$estimatedKb = [int]([math]::Ceiling($sizeBytes / 1KB))
$uninstallString = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' + $installedUninstall + '"'
$quietUninstallString = $uninstallString + ' -Quiet'

$stringProps = @{
    DisplayName            = "EarLab $version"
    DisplayVersion         = $version
    Publisher              = 'EarLab'
    InstallLocation        = $appDir
    DisplayIcon            = $installedExe
    UninstallString        = $uninstallString
    QuietUninstallString   = $quietUninstallString
    InstallDate            = (Get-Date -Format 'yyyyMMdd')
    EstimatedSize          = $estimatedKb
}
foreach ($name in $stringProps.Keys) {
    $type = if ($name -eq 'EstimatedSize') { 'DWord' } else { 'String' }
    New-ItemProperty -Path $uninstallKey -Name $name -Value $stringProps[$name] -PropertyType $type -Force | Out-Null
}
New-ItemProperty -Path $uninstallKey -Name NoModify -Value 1 -PropertyType DWord -Force | Out-Null
New-ItemProperty -Path $uninstallKey -Name NoRepair -Value 1 -PropertyType DWord -Force | Out-Null

if (-not $Quiet) {
    Write-Host "Installed EarLab $version"
    Write-Host "  app:       $appDir"
    Write-Host "  shortcut:  $startLnk"
    Write-Host "  uninstall: $installedUninstall"
    Write-Host 'User data in %APPDATA%\EarLab is not touched.'
}
