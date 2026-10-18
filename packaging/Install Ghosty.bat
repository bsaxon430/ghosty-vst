@echo off
title Install Ghosty
rem Ask Windows for permission (needed to write into Program Files)
net session >nul 2>&1
if %errorlevel% neq 0 (
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)
cd /d "%~dp0"
set "DEST=%CommonProgramFiles%\VST3"
echo.
echo   Installing Ghosty, Ghosty Beats, Ghosty Spectrum and Poltergeist...
echo   into %DEST%
echo.
if not exist "%DEST%" mkdir "%DEST%"
for /d %%P in ("VST3\*.vst3") do (
  if exist "%DEST%\%%~nxP" rmdir /s /q "%DEST%\%%~nxP"
  xcopy "%%P" "%DEST%\%%~nxP\" /e /i /q /y >nul
  echo   + %%~nP
)
echo.
echo   Done! Now open your DAW and rescan plug-ins:
echo     Ableton:   Options ^> Preferences ^> Plug-Ins ^> VST3 System Folders ON ^> Rescan
echo     FL Studio: Options ^> Manage plugins ^> Find more plugins
echo     Others:    rescan VST3 plug-ins
echo.
pause
