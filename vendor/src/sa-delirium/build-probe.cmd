@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -host_arch=x64 -arch=x86 >nul
if errorlevel 1 exit /b %errorlevel%
if not exist "%~dp0build" mkdir "%~dp0build"
pushd "%~dp0build"
cl /nologo /EHsc /O2 /std:c++20 /I"%~dp0..\delirium-physics\crates\delirium-sdk\include" "%~dp0EngineProbe.cpp" "%~dp0..\..\tools\cargo-target\i686-pc-windows-msvc\release\delirium_physics.dll.lib" /link /OUT:EngineProbe.exe
set build_result=%errorlevel%
if %build_result%==0 copy /y "%~dp0..\..\tools\cargo-target\i686-pc-windows-msvc\release\delirium_physics.dll" . >nul
popd
exit /b %build_result%
