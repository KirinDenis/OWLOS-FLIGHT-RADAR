@echo off
rem the how-to film for the site: tour.c plays touches on the screen code, ffmpeg makes the MP4
rem   TOUR.BAT              the 3.5" CYD (480x320, the board in the site's photo)
rem   TOUR.BAT ili9341      another screen (ili9341 st7796)
rem   TOUR.BAT promo        the short film for the posts, on the 5" glass: ..\..\shots\promo.mp4 and
rem                         promo.png, a frame of the LED lit, what the site and the README show
rem ffmpeg from C:\ffmpeg\bin, or set FFMPEG=path\ffmpeg.exe
rem writes ..\..\shots\<screen>\tour.mp4, its picture tour.png and its captions tour.vtt
setlocal
set DISP=%~1
if "%DISP%"=="" set DISP=st7796
set NUM=
if /i "%DISP%"=="ili9341" set NUM=1
if /i "%DISP%"=="st7796"  set NUM=2
if /i "%DISP%"=="promo"   set NUM=10
if "%NUM%"=="" (
  echo the film is made for ili9341, st7796 or promo, not %DISP%
  exit /b 1
)
set OUT=..\..\shots\%DISP%
set EXTRA=
set FONTS=..\..\core\font_small.c ..\..\core\font_medium.c ..\..\core\font_large.c ..\..\core\font_huge.c
if "%NUM%"=="10" (
  set OUT=build\promo
  set EXTRA=/DPROMO=1 /I..\..\core
  set FONTS=..\..\core\font_glass_small.c ..\..\core\font_glass_medium.c ..\..\core\font_glass_large.c ..\..\core\font_glass_huge.c ..\..\core\font_glass_tiny.c font_film_title.c font_film_big.c
)
if "%FFMPEG%"=="" set FFMPEG=C:\ffmpeg\bin\ffmpeg.exe
if not exist "%FFMPEG%" (
  echo ffmpeg not found: %FFMPEG%
  exit /b 1
)
set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" (
  echo Visual Studio 2022 C++ tools not found: %VCVARS%
  exit /b 1
)
call "%VCVARS%" >nul
cd /d "%~dp0"
if not exist build\tour_%DISP% mkdir build\tour_%DISP%
rem the map's tiles from OpenStreetMap, fetched once by curl (ground.inc)
if not exist tiles mkdir tiles
cl /nologo /O2 /W3 /utf-8 /std:c11 /D_CRT_SECURE_NO_WARNINGS /DRADAR_DISPLAY=%NUM% %EXTRA% ^
   /Fobuild\tour_%DISP%\ /Febuild\tour_%DISP%\tour.exe ^
   tour.c ..\..\core\sky.c ..\..\core\demo.c ..\..\core\gfx.c ..\..\core\font.c ^
   ..\..\core\ui.c ..\..\core\ui_scope.c ..\..\core\ui_pages.c ..\..\core\ui_wifi.c ..\..\core\ui_about.c ^
   ..\..\core\ui_weather.c ..\..\core\wx_alert.c ..\..\core\sun.c ^
   ..\..\core\qr_site.c ..\..\core\map.c ..\..\core\png_decode.c ..\..\core\watch.c ..\..\core\light.c ^
   %FONTS%
if errorlevel 1 exit /b 1
if not exist %OUT% mkdir %OUT%
build\tour_%DISP%\tour.exe "%FFMPEG%" %OUT%\tour.mp4 %OUT%\tour.png %OUT%\tour.vtt
if errorlevel 1 exit /b 1
if not "%NUM%"=="10" exit /b 0
copy /y %OUT%\tour.mp4 ..\..\shots\promo.mp4 >nul
"%FFMPEG%" -hide_banner -loglevel error -y -ss 50 -i ..\..\shots\promo.mp4 -frames:v 1 ..\..\shots\promo.png
