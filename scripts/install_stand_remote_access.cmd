@echo off
setlocal
cd /d "%~dp0"

net session >nul 2>&1
if errorlevel 1 (
  echo Administrator rights are required.
  echo Right-click install.cmd and select "Run as administrator".
  pause
  exit /b 1
)

if "%~1"=="" (
  powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0enable_stand_remote_access_win11.ps1" -UserName Azerty -AllowedClientAddress 192.168.0.171
) else (
  powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0enable_stand_remote_access_win11.ps1" %*
)
set "ORBITA_INSTALL_EXIT=%ERRORLEVEL%"
echo.
if not "%ORBITA_INSTALL_EXIT%"=="0" (
  echo INSTALLATION FAILED. Exit code: %ORBITA_INSTALL_EXIT%
) else (
  echo Installation completed successfully.
)
pause
exit /b %ORBITA_INSTALL_EXIT%
