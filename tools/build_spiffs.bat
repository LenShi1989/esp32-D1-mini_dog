@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_spiffs.ps1" %*
if errorlevel 1 (
  echo.
  echo Failed to build SPIFFS image.
  pause
  exit /b 1
)
endlocal
