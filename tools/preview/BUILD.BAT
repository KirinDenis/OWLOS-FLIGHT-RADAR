@echo off
rem build the PC preview of the flight radar's screen with MSVC and run it
rem   BUILD.BAT              the 2.8" CYD, screenshots go to ..\..\shots
rem   BUILD.BAT st7796       another screen (st7796 ili9486 gc9a01 crowadv50 esp4827s043), shots go to ..\..\shots\<screen>
rem core\ sources are the same as in the firmware
setlocal
set DISP=%~1
if "%DISP%"=="" set DISP=ili9341
set NUM=
if /i "%DISP%"=="ili9341" set NUM=1
if /i "%DISP%"=="st7796"  set NUM=2
if /i "%DISP%"=="ili9486" set NUM=3
if /i "%DISP%"=="gc9a01"  set NUM=4
if /i "%DISP%"=="crowadv50" set NUM=10
if /i "%DISP%"=="esp4827s043" set NUM=13
if "%NUM%"=="" (
  echo unknown screen %DISP%, pick one of: ili9341 st7796 ili9486 gc9a01 crowadv50 esp4827s043
  exit /b 1
)
set FONTS=..\..\core\font_small.c ..\..\core\font_medium.c ..\..\core\font_large.c ..\..\core\font_huge.c
if "%NUM%"=="10" set FONTS=..\..\core\font_glass_small.c ..\..\core\font_glass_medium.c ..\..\core\font_glass_large.c ..\..\core\font_glass_huge.c ..\..\core\font_glass_tiny.c
set OUT=..\..\shots
if not "%NUM%"=="1" set OUT=..\..\shots\%DISP%
set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
if not exist "%VCVARS%" (
  echo Visual Studio 2022 C++ tools not found: %VCVARS%
  exit /b 1
)
call "%VCVARS%" >nul
cd /d "%~dp0"
if not exist build\%DISP% mkdir build\%DISP%
rem the map's tiles from OpenStreetMap, fetched once by curl (ground.inc)
if not exist tiles mkdir tiles
rem the version, as the firmware takes it: the first "## " line of CHANGES.md
set VER=
for /f "tokens=2" %%v in ('findstr /b /c:"## " ..\..\CHANGES.md') do if not defined VER set VER=%%v
if "%VER%"=="" (
  echo no version in CHANGES.md: its first "## " line should be one, like ## 0.3
  exit /b 1
)
cl /nologo /O2 /W3 /utf-8 /std:c11 /D_CRT_SECURE_NO_WARNINGS /DRADAR_DISPLAY=%NUM% /DFLIGHT_VERSION=\"%VER%\" ^
   /Fobuild\%DISP%\ /Febuild\%DISP%\preview.exe ^
   preview.c ..\..\core\sky.c ..\..\core\demo.c ..\..\core\gfx.c ..\..\core\font.c ^
   ..\..\core\ui.c ..\..\core\ui_scope.c ..\..\core\ui_pages.c ..\..\core\ui_wifi.c ..\..\core\ui_about.c ^
   ..\..\core\ui_weather.c ..\..\core\wx_alert.c ..\..\core\sun.c ^
   ..\..\core\qr_site.c ..\..\core\map.c ..\..\core\png_decode.c ..\..\core\watch.c ..\..\core\light.c ^
   %FONTS%
if errorlevel 1 exit /b 1
if not exist %OUT% mkdir %OUT%
build\%DISP%\preview.exe %OUT%
