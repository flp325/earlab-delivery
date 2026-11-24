@echo off
set TMP=C:\deepseek\earlab\.tmp
set TEMP=C:\deepseek\earlab\.tmp
if not exist "%TMP%" mkdir "%TMP%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\deepseek\earlab\app
C:\deepseek\earlab\tools\cmake\bin\cmake.exe --build build-nmake --target EarLabEngine > C:\deepseek\earlab\artifacts\a5-app-build.log 2>&1
echo EXIT=%ERRORLEVEL% >> C:\deepseek\earlab\artifacts\a5-app-build.log
exit /b %ERRORLEVEL%
