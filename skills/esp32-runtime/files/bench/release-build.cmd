@echo off
rem release-build.cmd: does this board's firmware build from nothing, the way CI builds it? A fresh build
rem folder and a fresh sdkconfig from the defaults, no ccache: a default that only a fresh sdkconfig shows,
rem or a stale file a reused build folder hides, shows here.
rem
rem Run, from the firmware repository's root (the folder that holds firmware\):
rem   release-build.cmd <disp> <num> <target>
rem   release-build.cmd st7796 2 esp32        st7796, 2 and esp32 are examples: take the board's name,
rem                                           its RADAR_DISPLAY number and its chip from the CI's matrix
rem Needs: ESP-IDF 5.5. IDF_PATH and IDF_TOOLS_PATH if they are set, else the installer's own folders
rem   under %USERPROFILE%. A project whose CMake takes RADAR_DISPLAY, as OWLOS Flight Radar's does.
rem Changes: no board. It writes firmware\build_rel_<disp> and nothing else, and refuses a folder that is
rem   there already (delete it first). Keep build_* out of git.
rem Then: flash it with esp32-flash-and-settings\files\rigs\flash-one.ps1 -Dir firmware\build_rel_<disp>,
rem   or assemble the site from it as the workflow does, for a release rehearsal.
setlocal
set DISP=%~1
set NUM=%~2
set TARGET=%~3
if "%TARGET%"=="" (
  echo usage: release-build.cmd ^<disp^> ^<num^> ^<target^>
  exit /b 1
)
if not exist firmware\CMakeLists.txt (
  echo run it from the firmware repository's root: no firmware\CMakeLists.txt here
  exit /b 1
)
if "%IDF_PATH%"=="" set "IDF_PATH=%USERPROFILE%\esp\v5.5\esp-idf"
if "%IDF_TOOLS_PATH%"=="" set "IDF_TOOLS_PATH=%USERPROFILE%\.espressif"
for /d %%P in ("%IDF_TOOLS_PATH%\tools\idf-python\*") do set "PATH=%%P;%PATH%"
call "%IDF_PATH%\export.bat" >nul
if errorlevel 1 exit /b 1
cd /d firmware
if exist build_rel_%DISP% (
  echo build_rel_%DISP% is there already: not a fresh build
  exit /b 1
)
set "HERE=%CD:\=/%"
idf.py --no-ccache -B build_rel_%DISP% -DSDKCONFIG=%HERE%/build_rel_%DISP%/sdkconfig -DIDF_TARGET=%TARGET% -DRADAR_DISPLAY=%NUM% build
