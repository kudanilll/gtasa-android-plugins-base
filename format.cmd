@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0format.ps1"
if %ERRORLEVEL% neq 0 (
    echo [FORMAT] Formatting failed with error code %ERRORLEVEL%.
    pause
    exit /b %ERRORLEVEL%
)
