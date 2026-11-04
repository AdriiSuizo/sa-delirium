@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -host_arch=x64 -arch=x86 >nul
if errorlevel 1 exit /b %errorlevel%
pushd "%~dp0..\..\research\bike-transition-20261101"
cl /nologo /EHsc /std:c++20 "%~dp0TestBikeMotion.cpp" /Fo:TestBikeMotion.obj /Fe:TestBikeMotion.exe
if errorlevel 1 exit /b %errorlevel%
TestBikeMotion.exe
set test_result=%errorlevel%
popd
exit /b %test_result%
