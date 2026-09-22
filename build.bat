@echo off
REM Build BelfastPet.exe on Windows with CMake + Visual Studio (or MinGW if that is what CMake finds).
setlocal
cd /d "%~dp0"
cmake -S . -B build\win-native -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build\win-native --config Release || exit /b 1
echo.
echo Built: build\win-native\Release\BelfastPet.exe (MSVC) or build\win-native\BelfastPet.exe (MinGW)
endlocal
