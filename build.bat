@echo off
setlocal
set SRC=src\main.cpp src\app.cpp src\common.cpp src\config.cpp src\effects.cpp src\painter.cpp src\winenum.cpp
if not exist build mkdir build
windres res\app.rc -O coff -o build\app.res.o
if errorlevel 1 exit /b 1
g++ -std=c++17 -O1 -mwindows -static ^
    -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN ^
    %SRC% build\app.res.o -o build\BeterWindow.exe ^
    -ld2d1 -ldwrite -ldwmapi -luxtheme -lshell32 -lole32 -luuid -lgdi32 -luser32
if errorlevel 1 exit /b 1
echo Built build\BeterWindow.exe
