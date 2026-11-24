# EarLab A0 工具链检查
# 用法（任意 PowerShell 窗口）：
#   powershell -ExecutionPolicy Bypass -File .\verify-toolchain.ps1

$ErrorActionPreference = 'Continue'
$script:ok = $true

function Check {
    param([string]$Name, [bool]$Cond, [string]$Detail = '')
    if ($Cond) {
        Write-Host ("[PASS] {0,-34} {1}" -f $Name, $Detail) -ForegroundColor Green
    } else {
        Write-Host ("[FAIL] {0,-34} {1}" -f $Name, $Detail) -ForegroundColor Red
        $script:ok = $false
    }
}

Write-Host '=== EarLab A0 工具链检查 ===' -ForegroundColor Cyan

# --- Visual Studio Build Tools -------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
Check 'vswhere 存在' (Test-Path $vswhere) $vswhere

$vsPath = ''
if (Test-Path $vswhere) {
    $vsPath = (& $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath) 2>$null
}
Check 'C++ 工作负载已安装' ([bool]$vsPath) $vsPath

$cl = ''
if ($vsPath) {
    $msvc = Get-ChildItem (Join-Path $vsPath 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1
    if ($msvc) {
        $cl = Join-Path $msvc.FullName 'bin\Hostx64\x64\cl.exe'
    }
}
Check 'cl.exe 存在' (Test-Path $cl) $cl

$sdk = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\Include" -Directory -ErrorAction SilentlyContinue |
       Sort-Object Name -Descending | Select-Object -First 1
Check 'Windows SDK 存在' ([bool]$sdk) ($sdk.FullName)

# --- 构建工具 --------------------------------------------------------------
$portableCmake = Join-Path $PSScriptRoot '..\tools\cmake\bin\cmake.exe'
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$cmakePath = if (Test-Path $portableCmake) { $portableCmake } elseif ($cmakeCommand) { $cmakeCommand.Source } else { '' }
Check 'cmake 可用' ([bool]$cmakePath) $cmakePath
if ($cmakePath) { Write-Host ('       ' + (& $cmakePath --version | Select-Object -First 1)) }

# NMake is supplied by the required C++ workload; Ninja is not required.
$nmake = if ($cl) { Join-Path (Split-Path $cl -Parent) 'nmake.exe' } else { '' }
Check 'NMake 可用' ([bool]$nmake -and (Test-Path $nmake)) $nmake

# --- 依赖 ------------------------------------------------------------------
$juce = Join-Path $PSScriptRoot '..\third_party\JUCE\modules\juce_core'
Check 'JUCE 已就绪' (Test-Path $juce) (Resolve-Path (Join-Path $PSScriptRoot '..\third_party\JUCE') -ErrorAction SilentlyContinue)

# --- 结论 ------------------------------------------------------------------
Write-Host ''
if ($script:ok) {
    Write-Host 'GREEN: 工具链就绪，可以开始 A0 构建。' -ForegroundColor Green
    exit 0
} else {
    Write-Host 'RED: 还有缺失项，按上面 FAIL 行处理。' -ForegroundColor Red
    exit 1
}
