@echo off
setlocal EnableExtensions

set "EDGETX_REPO=D:\edgetx"
set "TOOLS=D:\edgetx\build-tools"
set "ARM_TOOLCHAIN_DIR=%TOOLS%\ArmGNU\bin"
set "VENV=%TOOLS%\.venv"
set "SYSROOT_INCLUDE=%TOOLS%\ArmGNU\arm-none-eabi\include"
set "GCC_BUILTIN_INCLUDE=%TOOLS%\ArmGNU\lib\gcc\arm-none-eabi\14.2.1\include"

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

cmake --preset pa01 -G Ninja -DCMAKE_BUILD_TYPE=Release -DARM_TOOLCHAIN_DIR="%ARM_TOOLCHAIN_DIR%"
if errorlevel 1 exit /b 1

cmake --build build\pa01 --target firmware --parallel 4
if errorlevel 1 exit /b 1

echo.
echo [OK] Firmware output:
dir /b build\pa01\firmware.*
endlocal
