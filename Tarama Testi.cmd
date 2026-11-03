@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\smoke-test.ps1"
pause
