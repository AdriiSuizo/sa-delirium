@echo off
setlocal
if not defined VSCMD_ARG_TGT_ARCH call "%~2" -arch=x86 -host_arch=x64
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x86" exit /b 3
pushd "%~1"
cl /nologo /LD /MT /O2 /EHsc /W4 /utf-8 /std:c++20 "%~dp0..\src\Handling.cpp" "%~dp0..\src\Plugin.cpp" "%~dp0..\src\SteamVehicle.cpp" "%~dp0..\src\SteamColour.cpp" "%~dp0..\src\SteamGamepad.cpp" "%~dp0..\src\SteamDisplay.cpp" /link bcrypt.lib user32.lib /OUT:SAStandalone.asi /Brepro
set "code=%errorlevel%"
popd
exit /b %code%
