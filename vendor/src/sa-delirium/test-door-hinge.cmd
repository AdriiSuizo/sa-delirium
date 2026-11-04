@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -host_arch=x64 -arch=x86 >nul
if errorlevel 1 exit /b %errorlevel%
if not exist "%~dp0build\door-hinge" mkdir "%~dp0build\door-hinge"
pushd "%~dp0build\door-hinge"
cl /nologo /EHsc /O2 /std:c++20 "%~dp0TestDoorHingeMotion.cpp" /Fe:TestDoorHingeMotion.exe
if errorlevel 1 (popd & exit /b 1)
TestDoorHingeMotion.exe
set result=%errorlevel%
popd
exit /b %result%
