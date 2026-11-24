@echo off
set TMP=C:\deepseek\earlab\.tmp
set TEMP=C:\deepseek\earlab\.tmp
if not exist "%TMP%" mkdir "%TMP%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\deepseek\earlab\app
C:\deepseek\earlab\tools\cmake\bin\cmake.exe -S . -B build-nmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release || exit /b 1
C:\deepseek\earlab\tools\cmake\bin\cmake.exe --build build-nmake --target EarLabPluginTests || exit /b 2
exit /b 0
