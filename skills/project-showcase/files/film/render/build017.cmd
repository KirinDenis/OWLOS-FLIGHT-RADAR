@echo off
rem the next version's screens for the film's look ahead, built from that version's branch without checking it out:
rem   mkdir ..\..\r017
rem   git -C %RADAR% archive 0.17 core tools/preview | tar -x -C ..\..\r017
rem   build017.cmd 1    the 2.8" CYD (320x240)
rem   build017.cmd 2    the 3.5" CYD (480x320)
rem film5.c built with HUD017 draws only what that version adds (its colour weather), nothing of the release's
rem needs: Visual Studio 2022 (cl), ffmpeg on PATH or FF set to it
setlocal
set NUM=%~1
if "%NUM%"=="" set NUM=10
set HERE=%~dp0
set R=%~dp0..\..\r017
set C=%R%\core
set P=%R%\tools\preview
if "%FF%"=="" set FF=ffmpeg.exe
set OUT=%HERE%..\clips
set FONTS=%C%\font_small.c %C%\font_medium.c %C%\font_large.c %C%\font_huge.c
if "%NUM%"=="10" set FONTS=%C%\font_glass_small.c %C%\font_glass_medium.c %C%\font_glass_large.c %C%\font_glass_huge.c %C%\font_glass_tiny.c
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%HERE%"
if not exist h017_%NUM% mkdir h017_%NUM%
if not exist tiles mkdir tiles
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /O2 /W3 /utf-8 /std:c11 /D_CRT_SECURE_NO_WARNINGS /DRADAR_DISPLAY=%NUM% /DFLIGHT_VERSION=\"0.17\" /DHUD017 /I%C% /I%P% ^
   /Foh017_%NUM%\ /Feh017_%NUM%\film5.exe film5.c ^
   %C%\sky.c %C%\demo.c %C%\gfx.c %C%\font.c %C%\ui.c %C%\ui_scope.c %C%\ui_pages.c %C%\ui_wifi.c %C%\ui_about.c ^
   %C%\ui_weather.c %C%\wx_alert.c %C%\sun.c %C%\qr_site.c %C%\map.c %C%\png_decode.c %C%\watch.c %C%\light.c ^
   %C%\paper.c %C%\ui_paper.c %FONTS% > h017_%NUM%\cl.txt 2>&1
if errorlevel 1 (
  type h017_%NUM%\cl.txt
  exit /b 1
)
findstr /c:"warning" h017_%NUM%\cl.txt
h017_%NUM%\film5.exe %FF% "%OUT%"
