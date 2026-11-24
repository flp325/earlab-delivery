# EarLab 一键构建 + 测试（A5）
# 采用 NMake 生成器：本机 CMake 4.4 + VS 2022 Build Tools 用 VS 生成器会在 SDK 探测时段错误。
# 用法：powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 [-WithPlugins] [-Clean]
# 头文件更新会自动触发全量重建；-Clean 强制全量重建。

param(
    [switch]$WithPlugins,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$root  = Split-Path $PSScriptRoot -Parent
$app   = Join-Path $root 'app'
$cmake = Join-Path $root 'tools\cmake\bin\cmake.exe'
$ctest = Join-Path $root 'tools\cmake\bin\ctest.exe'

if (-not (Test-Path $cmake)) {
    throw "缺少 CMake：$cmake。先运行 python scripts/fetch_cmake.py"
}

# 头文件改了但增量构建没重编所有依赖时，类布局会不一致（表现为随机崩溃）。
# 这里检测头文件时间戳，必要时自动全量重建。
$buildDir = Join-Path $app 'build-nmake'
$stamp    = Join-Path $buildDir '.stamp'

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host 'Clean：删除 build-nmake 后全量重建…'
    Remove-Item $buildDir -Recurse -Force
}
elseif (Test-Path $buildDir) {
    $newestHeader = Get-ChildItem -LiteralPath (Join-Path $app 'src') -Recurse -File -Filter '*.h' |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1

    $stampTime = if (Test-Path $stamp) { (Get-Item $stamp).LastWriteTime } else { [datetime]::MinValue }

    if ($newestHeader -and $newestHeader.LastWriteTime -gt $stampTime) {
        Write-Host ('头文件有更新（' + $newestHeader.Name + '），自动全量重建…')
        Remove-Item $buildDir -Recurse -Force
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw '找不到 vswhere.exe，VS Build Tools 是否已安装？' }

$vsPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

if (-not $vsPath) { throw '找不到包含 C++ 工作负载的 Visual Studio' }

$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "找不到 vcvars64.bat：$vcvars" }

$tmp = Join-Path $root '.tmp'
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$bat = Join-Path $tmp 'earlab_build.bat'
@"
@echo off
set VSLANG=1033
set TMP=$tmp
set TEMP=$tmp
call "$vcvars" >nul
cd /d "$app"
"$cmake" -S . -B build-nmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release || exit /b 1
"$cmake" --build build-nmake || exit /b 2
echo.
echo === CTest: DSP / Engine / Plugin ===
"$ctest" --test-dir build-nmake --output-on-failure || exit /b 3
echo.
echo === Loudness trim check ===
"$app\build-nmake\EarLabEngine_artefacts\Release\EarLab Engine.exe" --check-trims || exit /b 5
exit /b 0
"@ | Set-Content -Path $bat -Encoding ASCII

try {
    & cmd /c $bat
    $code = $LASTEXITCODE
}
finally {
    Remove-Item $bat -Force -ErrorAction SilentlyContinue
}

if ($code -ne 0) { throw "构建或测试失败，退出码 $code" }

# 记录本次构建时间，供下次判断是否需要因头文件变化而全量重建
Set-Content -Path $stamp -Value (Get-Date -Format o) -Encoding ASCII

# 本地开发签名：消除 Windows「未知发布者 / 安全警告」弹窗。
# 未安装开发证书时脚本会静默跳过，不影响构建。
$signer = Join-Path $PSScriptRoot 'sign-app.ps1'
if (Test-Path $signer) {
    & $signer
}

if ($WithPlugins) {
    $pluginTest = Join-Path $app 'build-nmake\EarLabPluginTests_artefacts\Release\EarLabPluginTests.exe'
    Write-Host ''
    Write-Host '=== VST3 plugin scan ==='
    & $pluginTest
    if ($LASTEXITCODE -ne 0) { throw "插件测试失败，退出码 $LASTEXITCODE" }
}

Write-Host ''
Write-Host 'GREEN: 构建与测试全部通过' -ForegroundColor Green
Write-Host ('应用: ' + (Join-Path $app 'build-nmake\EarLabEngine_artefacts\Release\EarLab Engine.exe'))
