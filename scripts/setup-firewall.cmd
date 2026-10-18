@echo off
setlocal
net session >nul 2>&1
if not "%errorlevel%"=="0" (
    echo Requesting administrator privileges...
    powershell -NoProfile -Command "$a='%*'; if($a){Start-Process -FilePath '%~f0' -Verb RunAs -ArgumentList $a}else{Start-Process -FilePath '%~f0' -Verb RunAs}"
    exit /b
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-firewall.ps1" %*
set RC=%errorlevel%
if "%~1"=="" pause
exit /b %RC%
