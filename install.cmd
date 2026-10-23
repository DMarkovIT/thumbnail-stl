@echo off
setlocal
set "DIR=%~dp0build\bin\Release"
"%DIR%\stlprev-setup.exe" register "%DIR%\STLPreview.dll"
if errorlevel 1 (
  echo Registration failed.
  pause
  exit /b 1
)
echo Registered.
pause
