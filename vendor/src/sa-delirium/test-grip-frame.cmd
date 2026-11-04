@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -host_arch=x64 -arch=x86 >nul
if errorlevel 1 exit /b %errorlevel%
if not exist "%~dp0build\grip-frame" mkdir "%~dp0build\grip-frame"
pushd "%~dp0build\grip-frame"
cl /nologo /EHsc /O2 /std:c++20 "%~dp0TestGripFrameMath.cpp" /Fe:TestGripFrameMath.exe
if errorlevel 1 (popd & exit /b 1)
TestGripFrameMath.exe
set result=%errorlevel%
popd
exit /b %result%
