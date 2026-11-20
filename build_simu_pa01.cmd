@echo off
setlocal EnableExtensions

set "EDGETX_REPO=D:\edgetx"
set "TOOLS=D:\edgetx\build-tools"
set "SDL2_ROOT=%TOOLS%\SDL2-2.32.10"
set "VENV=%TOOLS%\.venv"

set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" (
  echo [ERROR] Visual Studio 2022 VsDevCmd.bat not found.
  exit /b 1
)

set "VSROOT=%VSDEVCMD:~0,-27%"
set "PATH=%VENV%\Scripts;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%SDL2_ROOT%\lib\x64;%PATH%"

call "%VSDEVCMD%" -startdir=none -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1

rem Make UCRT/Windows SDK headers and libs explicit (VsDevCmd sometimes misses them)
set "WINKITS=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER="
for /d %%D in ("%WINKITS%\Include\*") do set "SDKVER=%%~nxD"
if not defined SDKVER (
  echo [ERROR] Windows SDK not found.
  exit /b 1
)
set "INCLUDE=%VCINSTALLDIR%include;%WINKITS%\Include\%SDKVER%\ucrt;%WINKITS%\Include\%SDKVER%\um;%WINKITS%\Include\%SDKVER%\shared;%WINKITS%\Include\%SDKVER%\winrt;%INCLUDE%"
set "LIB=%VCINSTALLDIR%lib\x64;%WINKITS%\Lib\%SDKVER%\ucrt\x64;%WINKITS%\Lib\%SDKVER%\um\x64;%LIB%"

call "%VENV%\Scripts\activate.bat"
if errorlevel 1 exit /b 1

cd /d "%EDGETX_REPO%"

cmake --preset simu -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPCB=PA01 -DTRANSLATIONS=CN -DDISABLE_COMPANION=ON -DSDL2_DIR="%SDL2_ROOT%\cmake"
if errorlevel 1 exit /b 1

cmake --build build\simu --target simu --parallel 4
if errorlevel 1 exit /b 1

echo.
echo [OK] Simulator:
dir /b build\simu\simu.exe
endlocal
