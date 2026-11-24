@echo off
rem EarLab portable launcher.
rem Keeps the profile (and assessments/plugin data) next to this file,
rem so deleting the extracted folder removes the app and its data.
setlocal
start "" "%~dp0EarLab Engine.exe" --profile="%~dp0profile.json"
endlocal
