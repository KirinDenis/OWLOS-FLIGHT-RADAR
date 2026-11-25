@echo off
rem the film's screens: film5.c built with core\ for one display and run, its clips and stills into ..\clips
rem   build.cmd 10      the 5" glass (800x480)
rem   build.cmd 1       the 2.8" CYD (320x240)
rem   build.cmd 2       the 3.5" CYD (480x320)
rem needs: Visual Studio 2022 (cl), ffmpeg on PATH or FF set to it, RADAR set to an OWLOS-FLIGHT-RADAR checkout
rem the map's tiles come from OpenStreetMap once, into tiles\ here (tools\preview\ground.inc)
setlocal
set NUM=%~1
if "%NUM%"=="" set NUM=10
set HERE=%~dp0
if "%RADAR%"=="" (
  echo set RADAR to your OWLOS-FLIGHT-RADAR checkout first
  exit /b 1
)
set R=%RADAR%
set C=%R%\core
set P=%R%\tools\preview
if "%FF%"=="" set FF=ffmpeg.exe
set OUT=%HERE%..\clips
set FONTS=%C%\font_small.c %C%\font_medium.c %C%\font_large.c %C%\font_huge.c
if "%NUM%"=="10" set FONTS=%C%\font_glass_small.c %C%\font_glass_medium.c %C%\font_glass_large.c %C%\font_glass_huge.c %C%\font_glass_tiny.c
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%HERE%"
if not exist build%NUM% mkdir build%NUM%
if not exist tiles mkdir tiles
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /O2 /W3 /utf-8 /std:c11 /D_CRT_SECURE_NO_WARNINGS /DRADAR_DISPLAY=%NUM% /DFLIGHT_VERSION=\"0.16\" /I%C% /I%P% ^
   /Fobuild%NUM%\ /Febuild%NUM%\film5.exe film5.c ^
   %C%\sky.c %C%\demo.c %C%\gfx.c %C%\font.c %C%\ui.c %C%\ui_scope.c %C%\ui_pages.c %C%\ui_wifi.c %C%\ui_about.c ^
   %C%\ui_weather.c %C%\wx_alert.c %C%\sun.c %C%\qr_site.c %C%\map.c %C%\png_decode.c %C%\watch.c %C%\light.c ^
   %FONTS% > build%NUM%\cl.txt 2>&1
if errorlevel 1 (
  type build%NUM%\cl.txt
  exit /b 1
)
findstr /c:"warning" build%NUM%\cl.txt
build%NUM%\film5.exe %FF% "%OUT%"
