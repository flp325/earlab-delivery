# 截取 EarLab Engine 窗口，用于验收留档
param(
    [string]$Exe = 'C:\deepseek\earlab\app\build-nmake\EarLabEngine_artefacts\Release\EarLab Engine.exe',
    [string]$Out = 'C:\deepseek\earlab\artifacts\a2-window.png',
    [string]$Arguments = '',
    [int]$WaitSeconds = 4
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;

public static class WinCap
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }

    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@

# PowerShell 默认不是 DPI 感知的：在 150% 缩放下 GetWindowRect 返回虚拟化尺寸，
# 截图会被裁切。先声明 DPI 感知，拿到真实像素尺寸。
[WinCap]::SetProcessDPIAware() | Out-Null

New-Item -ItemType Directory -Force -Path (Split-Path $Out) | Out-Null

$log = Join-Path $env:TEMP 'earlab-gui.log'
$p = Start-Process -FilePath $Exe -ArgumentList $Arguments -PassThru `
        -RedirectStandardOutput $log -RedirectStandardError ($log + '.err')

Start-Sleep -Seconds $WaitSeconds
$p.Refresh()

if ($p.HasExited) { throw "程序提前退出，exit=$($p.ExitCode)" }

$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) {
    Start-Sleep -Seconds 2
    $p.Refresh()
    $h = $p.MainWindowHandle
}
if ($h -eq [IntPtr]::Zero) {
    $p | Stop-Process -Force
    throw '无法获取主窗口句柄'
}

[WinCap]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 600

$rect = New-Object -TypeName 'WinCap+RECT'
[WinCap]::GetWindowRect($h, [ref]$rect) | Out-Null
$width  = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top

$bmp = New-Object System.Drawing.Bitmap $width, $height
$g   = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok  = [WinCap]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
$g.Dispose()

$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$p | Stop-Process -Force

Write-Host "saved=$Out size=${width}x${height} printwindow=$ok"
