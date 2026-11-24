@echo off
rem EarLab route C installer wrapper (no administrator required).
rem Extracted portable ZIP -> run this file, or run install.ps1 directly.
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
set CODE=%ERRORLEVEL%
if not "%CODE%"=="0" (
    echo.
    echo EarLab install failed with exit code %CODE%.
    pause
    exit /b %CODE%
)
echo.
echo EarLab installed. Press any key to close.
pause >nul
exit /b 0
