@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build-test.ps1"
pause
