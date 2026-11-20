@echo off
setlocal EnableExtensions

REM Build FlySky PA01 firmware with Simplified Chinese (CN) UI.
REM Uses a separate build dir so the English build cache is untouched.

set "EDGETX_REPO=D:\edgetx"
set "TOOLS=D:\edgetx\build-tools"
set "ARM_TOOLCHAIN_DIR=%TOOLS%\ArmGNU\bin"
set "VENV=%TOOLS%\.venv"

set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"
if not exist "%VSDEVCMD%" (
  echo [ERROR] Visual Studio 2022 VsDevCmd.bat not found.
  exit /b 1
)

set "VSROOT=%VSDEVCMD:~0,-27%"
set "PATH=%ARM_TOOLCHAIN_DIR%;%VENV%\Scripts;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

call "%VSDEVCMD%" -startdir=none -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1

call "%VENV%\Scripts\activate.bat"
if errorlevel 1 exit /b 1

cd /d "%EDGETX_REPO%"

cmake -S . -B build\pa01-cn -G Ninja -DCMAKE_TOOLCHAIN_FILE="%EDGETX_REPO%/cmake/toolchain/arm-none-eabi.cmake" -DCMAKE_BUILD_TYPE=Release -DEdgeTX_SUPERBUILD=FALSE -DNATIVE_BUILD=FALSE "-DFETCHCONTENT_BASE_DIR=%EDGETX_REPO%/.cache/fetchcontent" -DPCB=PA01 "-DARM_TOOLCHAIN_DIR=%ARM_TOOLCHAIN_DIR%" -DTRANSLATIONS=CN
if errorlevel 1 exit /b 1

cmake --build build\pa01-cn --target firmware --parallel 4
if errorlevel 1 exit /b 1

echo.
echo [OK] Firmware output (Chinese):
dir /b build\pa01-cn\firmware.*
endlocal
