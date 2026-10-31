@echo off
setlocal
cd /d "%~dp0.."
if not exist build mkdir build

windres -I frontend frontend\frontend.rc -O coff -o build\frontend.res
if errorlevel 1 goto :fail

g++ -std=c++17 -O2 -Wall -Wextra -Wno-cast-function-type -mwindows -DUNICODE -D_UNICODE ^
    frontend\main.cpp frontend\app.cpp frontend\ui.cpp frontend\ui_about.cpp ^
    frontend\pipe_client.cpp frontend\focus.cpp build\frontend.res ^
    -o build\frontend.exe ^
    -luser32 -lgdi32 -lshell32 ^
    -static -static-libgcc -static-libstdc++
if errorlevel 1 goto :fail

echo Built build\frontend.exe
exit /b 0

:fail
echo Build failed.
exit /b 1
