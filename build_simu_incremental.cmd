@echo off
setlocal EnableExtensions

REM Incremental rebuild of simu.exe after editing a single source file.
REM Skips the slow "cmake --fresh" reconfigure used by the full script.

set "EDGETX_REPO=D:\edgetx"
set "TOOLS=D:\edgetx\build-tools"
set "MINGW=%TOOLS%\mingw64"
set "VENV=%TOOLS%\.venv"
set "VSCMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "SDL2_ROOT=%TOOLS%\SDL2-2.32.10"

REM MinGW first so Ninja picks gcc/g++, SDL2 dll dir for runtime linkage.
set "PATH=%MINGW%\bin;%VENV%\Scripts;%VSCMAKE%\CMake\bin;%VSCMAKE%\Ninja;%SDL2_ROOT%\lib\x64;%PATH%"

call "%VENV%\Scripts\activate.bat"
if errorlevel 1 exit /b 1

cd /d "%EDGETX_REPO%"

cmake --build build\simu --target simu --parallel 4
if errorlevel 1 (
  echo.
  echo [ERROR] Incremental build failed.
  exit /b 1
)

echo.
echo [OK] Simulator rebuilt:
dir build\simu\simu.exe
endlocal
