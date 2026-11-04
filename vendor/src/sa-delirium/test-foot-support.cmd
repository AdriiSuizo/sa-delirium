@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -host_arch=x64 -arch=x86 >nul
if errorlevel 1 exit /b %errorlevel%
pushd "%~dp0build"
cl /nologo /EHsc /std:c++20 "%~dp0TestFootSupportMath.cpp" /Fe:TestFootSupportMath.exe
if errorlevel 1 exit /b %errorlevel%
TestFootSupportMath.exe
set test_result=%errorlevel%
popd
exit /b %test_result%
