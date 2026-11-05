@echo off
setlocal
call "%~2" -arch=x86 -host_arch=x64 >nul
if errorlevel 1 exit /b 2
cl /nologo /MT /O2 /EHsc /std:c++17 /D_CRT_SECURE_NO_WARNINGS "%~dp0RendererProbe.cpp" /Fo"%~1\RendererProbe.obj" /Fe"%~1\RendererProbe.exe" /link user32.lib /Brepro
exit /b %errorlevel%
