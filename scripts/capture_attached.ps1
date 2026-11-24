param(
    [string]$ProcessName = 'EarLab Engine',
    [string]$WindowTitle = '',
    [int]$WindowIndex = -1,
    [string]$Out = 'C:\deepseek\earlab\artifacts\attach-window.png',
    [int]$WaitSeconds = 30
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class AttachCap
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();

    public static IntPtr FindTopLevel(uint pid, string title, int index)
    {
        IntPtr found = IntPtr.Zero;
        int seen = 0;

        EnumWindows((h, l) =>
        {
            uint p;
            GetWindowThreadProcessId(h, out p);

            if (p == pid && IsWindowVisible(h))
            {
                var sb = new StringBuilder(256);
                GetWindowText(h, sb, 256);
                var text = sb.ToString();

                bool matches = title == "" ? text.Length > 0 : text.Contains(title);

                if (matches)
                {
                    if (index < 0 || seen == index)
                    {
                        found = h;
                        return false;
                    }

                    ++seen;
                }
            }

            return true;
        }, IntPtr.Zero);

        return found;
    }
}
"@

[AttachCap]::SetProcessDPIAware() | Out-Null

New-Item -ItemType Directory -Force -Path (Split-Path $Out) | Out-Null
Start-Sleep -Seconds $WaitSeconds

$procs = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
if ($procs.Count -eq 0) { throw "进程未运行: $ProcessName" }

# 扫描子进程同名但没有窗口，遍历所有同名进程找真正有窗口的那个
$h = [IntPtr]::Zero

foreach ($proc in $procs) {
    $h = [AttachCap]::FindTopLevel([uint32]$proc.Id, $WindowTitle, $WindowIndex)
    if ($h -ne [IntPtr]::Zero) { break }
}

if ($h -eq [IntPtr]::Zero) { throw "找不到窗口: [$WindowTitle] index=$WindowIndex" }

[AttachCap]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500

$rect = New-Object -TypeName 'AttachCap+RECT'
[AttachCap]::GetWindowRect($h, [ref]$rect) | Out-Null
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top

$bmp = New-Object System.Drawing.Bitmap $width, $height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [AttachCap]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()

Write-Host "saved=$Out size=${width}x${height} printwindow=$ok"
