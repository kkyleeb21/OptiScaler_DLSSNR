@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Uninstall-D18.ps1" %*
set "D18_EXIT=%ERRORLEVEL%"
echo.
if not "%D18_EXIT%"=="0" echo Uninstall did not complete. Exit code: %D18_EXIT%
pause
exit /b %D18_EXIT%
