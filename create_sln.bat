@echo off
cd /d %~dp0

echo Running CMake...
echo/
cmake -B build -G "Visual Studio 17 2022" -A "Win32"
echo/
echo Assuming CMake setup went well, the project sln is located at "build/xzptool.sln".
echo/
pause
