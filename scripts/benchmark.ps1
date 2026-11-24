param([string]$Baseline = 'v0.6.1')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$baselineDir = Join-Path $root '.tmp\perf-baseline'
New-Item -ItemType Directory -Force -Path $baselineDir | Out-Null

# Never check out or modify the release tree. Only rename classes in temporary
# source copies so baseline and current implementations run in the same process.
foreach ($module in @('MultiBandEq', 'MaterialPlayer', 'CompressorProcessor')) {
    foreach ($extension in @('h', 'cpp')) {
        $spec = '{0}:app/src/engine/{1}.{2}' -f $Baseline, $module, $extension
        $source = & git -C $root show $spec
        if ($LASTEXITCODE -ne 0) { throw "Cannot read baseline $spec" }
        $sourceText = ($source -join "`n").Replace($module, "Baseline$module")
        [IO.File]::WriteAllText((Join-Path $baselineDir "Baseline$module.$extension"), $sourceText, [Text.UTF8Encoding]::new($false))
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'C++ build tools missing' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
$cmake = Join-Path $root 'tools\cmake\bin\cmake.exe'
$batch = Join-Path $baselineDir 'benchmark.bat'
@"
@echo off
set VSLANG=1033
set TMP=$baselineDir
set TEMP=$baselineDir
call "$vcvars" >nul
"$cmake" -S "$root\app" -B "$root\app\build-nmake" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DEARLAB_BUILD_BENCHMARKS=ON "-DEARLAB_PERF_BASELINE_DIR=$baselineDir" || exit /b 1
"$cmake" --build "$root\app\build-nmake" --target EarLabPerformance || exit /b 2
"$root\app\build-nmake\EarLabPerformance_artefacts\Release\EarLabPerformance.exe"
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $batch -Encoding ASCII
& cmd /c $batch
if ($LASTEXITCODE -ne 0) { throw 'Benchmark failed' }
