@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check-env.ps1" %*
set RC=%errorlevel%
if "%~1"=="" pause
exit /b %RC%
