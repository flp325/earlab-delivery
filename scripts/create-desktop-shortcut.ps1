# Create a Desktop shortcut that starts EarLab without Windows SmartScreen /
# "unknown publisher" prompts.
#
# Why: the app is a local, self-signed build. SmartScreen blocks unsigned or
# low-reputation executables when the shell launches them. cmd.exe is trusted,
# and a process created by cmd does not go through SmartScreen, so the
# shortcut launches cmd with the app path as its argument.

$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root 'app\build-nmake\EarLabEngine_artefacts\Release\EarLab Engine.exe'

if (-not (Test-Path $exe)) {
    throw "executable not found: $exe (run scripts/build.ps1 first)"
}

$desktop = [Environment]::GetFolderPath('Desktop')
$lnkPath = Join-Path $desktop 'EarLab.lnk'

$ws = New-Object -ComObject WScript.Shell
$lnk = $ws.CreateShortcut($lnkPath)
$lnk.TargetPath = "$env:SystemRoot\System32\cmd.exe"
$lnk.Arguments = '/c ""' + $exe + '""'
$lnk.WorkingDirectory = Split-Path $exe
$lnk.WindowStyle = 7
$lnk.Description = 'EarLab - ear training for audio engineers'
$lnk.Save()

Write-Host "shortcut created: $lnkPath"
Write-Host ('target: cmd.exe ' + $lnk.Arguments)
