@echo off
setlocal
set "DELTA_ARCH=%~3"
if not defined DELTA_ARCH set "DELTA_ARCH=x64"
if /I not "%DELTA_ARCH%"=="x64" if /I not "%DELTA_ARCH%"=="x86" exit /b 2
call "%~2" -arch=%DELTA_ARCH% -host_arch=x64 > "%~1\delta-compiler-environment.log" 2>&1
if errorlevel 1 exit /b 2
cl /nologo /MT /O2 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /Fo"%~1\DeltaTool.obj" "%~dp0DeltaTool.cpp" /link msdelta.lib /OUT:"%~1\DeltaTool.exe" /Brepro
exit /b %errorlevel%
