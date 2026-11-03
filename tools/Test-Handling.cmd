@echo off
setlocal
if not defined VSCMD_ARG_TGT_ARCH call "%~2" -arch=x86 -host_arch=x64
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x86" exit /b 3
pushd "%~1"
cl /nologo /MT /O2 /EHsc /W4 /std:c++20 "%~dp0..\src\Handling.cpp" "%~dp0..\tests\HandlingTests.cpp" /link /OUT:HandlingTests.exe
if errorlevel 1 (popd & exit /b 1)
HandlingTests.exe
set "code=%errorlevel%"
popd
exit /b %code%
