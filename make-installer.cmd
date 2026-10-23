@echo off
setlocal
cd /d "%~dp0"

cmake --build build --config Release
if errorlevel 1 exit /b 1

set "NSIS=%SCOOP%\apps\nsis\current\makensis.exe"
if not exist "%NSIS%" set "NSIS=%USERPROFILE%\scoop\apps\nsis\current\makensis.exe"
if not exist "%NSIS%" (
  where makensis >nul 2>nul
  if errorlevel 1 (
    echo makensis not found. Install NSIS 3 first.
    exit /b 1
  )
  set "NSIS=makensis"
)

if not exist "dist" mkdir "dist"
"%NSIS%" /V3 /DSOURCE_DIR="%~dp0build\bin\Release" /DOUT_DIR="%~dp0dist" "%~dp0installer\STLPreview.nsi"
if errorlevel 1 exit /b 1

echo Installer: dist\STLPreview-Setup-1.0.0.exe
