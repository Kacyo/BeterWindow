@echo off
setlocal
set CXXFLAGS=-std=c++17 -O1 -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN
set LIBINC=library\include
set UISRC=library\src\painter.cpp library\src\theme.cpp library\src\widgets.cpp library\src\window.cpp
set SRC=src\main.cpp src\app.cpp src\common.cpp src\config.cpp src\effects.cpp src\winenum.cpp
if not exist build mkdir build

echo Building Kacyo.BeterWindow.UI.dll ...
g++ %CXXFLAGS% -shared -static-libgcc -static-libstdc++ -DBWUI_BUILD -I%LIBINC% ^
    %UISRC% -o build\Kacyo.BeterWindow.UI.dll ^
    -Wl,--out-implib,build\libKacyo.BeterWindow.UI.dll.a ^
    -ld2d1 -ldwrite -ldwmapi -lgdi32 -luser32 -lole32 -luuid
if errorlevel 1 exit /b 1

echo Building BeterWindow.exe ...
windres res\app.rc -O coff -o build\app.res.o
if errorlevel 1 exit /b 1
g++ %CXXFLAGS% -mwindows -static -I%LIBINC% ^
    %SRC% build\app.res.o build\libKacyo.BeterWindow.UI.dll.a -o build\BeterWindow.exe ^
    -ld2d1 -ldwrite -ldwmapi -luxtheme -lshell32 -lole32 -luuid -lgdi32 -luser32
if errorlevel 1 exit /b 1

echo Built build\Kacyo.BeterWindow.UI.dll
echo Built build\BeterWindow.exe
