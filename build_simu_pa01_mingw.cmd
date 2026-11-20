@echo off
setlocal EnableExtensions

set "EDGETX_REPO=D:\edgetx"
set "TOOLS=D:\edgetx\build-tools"
set "MINGW=%TOOLS%\mingw64"
set "SDL2_ROOT=%TOOLS%\SDL2-2.32.10"
set "VENV=%TOOLS%\.venv"
set "VSCMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake"

if not exist "%MINGW%\bin\gcc.exe" (
  echo [ERROR] MinGW not extracted yet: %MINGW%\bin\gcc.exe
  exit /b 1
)

rem Put MinGW first so CMake picks gcc/g++ (NOT MSVC)
set "PATH=%MINGW%\bin;%VENV%\Scripts;%VSCMAKE%\CMake\bin;%VSCMAKE%\Ninja;%SDL2_ROOT%\lib\x64;%PATH%"
set "CC=%MINGW%\bin\gcc.exe"
set "CXX=%MINGW%\bin\g++.exe"

call "%VENV%\Scripts\activate.bat"
if errorlevel 1 exit /b 1

cd /d "%EDGETX_REPO%"

rem Clean config so compiler cache is not reused from MSVC attempt
cmake --preset simu -G Ninja -DCMAKE_BUILD_TYPE=Debug -DPCB=PA01 -DTRANSLATIONS=CN -DDISABLE_COMPANION=ON -DSDL2_DIR="%SDL2_ROOT%\cmake" -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="%EDGETX_REPO%\.cache\fetchcontent\googletest-src" -DFETCHCONTENT_SOURCE_DIR_IMGUI="%EDGETX_REPO%\.cache\fetchcontent\imgui-src" -DCMAKE_C_COMPILER="%MINGW%\bin\gcc.exe" -DCMAKE_CXX_COMPILER="%MINGW%\bin\g++.exe" --fresh
if errorlevel 1 exit /b 1

cmake --build build\simu --target simu --parallel 4
if errorlevel 1 exit /b 1

echo.
echo [OK] Simulator built:
dir /b build\simu\simu.exe
endlocal
