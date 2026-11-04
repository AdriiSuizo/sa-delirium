@echo off
setlocal
call "%~2" -arch=x86 -host_arch=x64 > "%~1\compiler-environment.log" 2>&1
if errorlevel 1 exit /b 2
cl /nologo /MT /O2 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /Fo"%~1\SteamLauncher.obj" "%~dp0SteamLauncher.cpp" /link user32.lib /SUBSYSTEM:WINDOWS /OUT:"%~1\SteamLauncher.exe" /Brepro
exit /b %errorlevel%
