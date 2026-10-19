@echo off
setlocal EnableExtensions
title Ghosty installer

:: ---- ask for admin rights once (needed to install tools and the plugins) ----
net session >nul 2>&1
if errorlevel 1 (
  echo Asking Windows for permission...
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)
cd /d "%~dp0"

echo.
echo    ghosty  -  installing Ghosty + Ghosty Beats for Ableton
echo    -------------------------------------------------------
echo    This can take 20-40 minutes the first time. Leave this window open.
echo.

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"

if not defined VSPATH (
  echo [1/4] Downloading Microsoft's C++ build tools ^(free, about 2-3 GB^)...
  curl.exe -L --fail -o "%TEMP%\vs_BuildTools.exe" https://aka.ms/vs/17/release/vs_BuildTools.exe
  if errorlevel 1 goto netfail
  echo       Installing them. A Visual Studio progress window will appear - just let it finish.
  "%TEMP%\vs_BuildTools.exe" --passive --wait --norestart --nocache ^
    --add Microsoft.VisualStudio.Workload.VCTools ^
    --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ^
    --add Microsoft.VisualStudio.Component.VC.CMake.Project ^
    --add Microsoft.VisualStudio.Component.Windows11SDK.22621 ^
    --includeRecommended
  for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
) else (
  echo [1/4] C++ build tools already installed.
)
if not defined VSPATH goto vsfail

set "CMAKE=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" (
  where cmake >nul 2>nul && set "CMAKE=cmake"
)
if not exist "%CMAKE%" if not "%CMAKE%"=="cmake" goto vsfail

echo [2/4] Setting up the project (downloads the JUCE toolkit)...
"%CMAKE%" -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto buildfail

echo [3/4] Building the plugins (5-15 minutes)...
"%CMAKE%" --build build --config Release --parallel
if errorlevel 1 goto buildfail

echo [4/4] Putting the plugins where Ableton looks...
set "DEST=%CommonProgramFiles%\VST3"
if not exist "%DEST%" mkdir "%DEST%"
xcopy /E /I /Y /Q "build\GhostySynth_artefacts\Release\VST3\Ghosty.vst3" "%DEST%\Ghosty.vst3" >nul || goto copyfail
xcopy /E /I /Y /Q "build\GhostyBeats_artefacts\Release\VST3\Ghosty Beats.vst3" "%DEST%\Ghosty Beats.vst3" >nul || goto copyfail
xcopy /E /I /Y /Q "build\GhostySpectrum_artefacts\Release\VST3\Ghosty Spectrum.vst3" "%DEST%\Ghosty Spectrum.vst3" >nul || goto copyfail
xcopy /E /I /Y /Q "build\Poltergeist_artefacts\Release\VST3\Poltergeist.vst3" "%DEST%\Poltergeist.vst3" >nul || goto copyfail
xcopy /E /I /Y /Q "build\MrMickens_artefacts\Release\VST3\Mr Mickens.vst3" "%DEST%\Mr Mickens.vst3" >nul || goto copyfail

echo.
echo    =====================================================
echo     DONE!  Now open Ableton:
echo       Options ^> Preferences ^> Plug-Ins ^> Rescan
echo     Then find them under  Plug-Ins ^> VST3 ^> GhostBlock
echo    =====================================================
echo.
pause
exit /b 0

:netfail
echo.
echo [!] Couldn't download the build tools. Check the internet connection and run this again.
pause & exit /b 1
:vsfail
echo.
echo [!] The C++ build tools didn't finish installing. Restart the PC and run INSTALL-GHOSTY.bat again.
pause & exit /b 1
:buildfail
echo.
echo [!] The build hit an error. Open Claude Code in this folder and say:
echo     "Run INSTALL-GHOSTY.bat and fix any build errors."
pause & exit /b 1
:copyfail
echo.
echo [!] Couldn't copy the plugins into %DEST%. Run this file again and click Yes on the permission popup.
pause & exit /b 1
