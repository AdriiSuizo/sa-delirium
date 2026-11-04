@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -host_arch=x64 -arch=x86 >nul
if errorlevel 1 exit /b %errorlevel%
python "%~dp0verify-native-abi.py"
if errorlevel 1 exit /b %errorlevel%
if not exist "%~dp0build" mkdir "%~dp0build"
pushd "%~dp0build"
cl /nologo /LD /EHsc /O2 /std:c++20 /D_CRT_SECURE_NO_WARNINGS /I"%~dp0..\plugin-sdk-sa\plugin_sa\game_sa\rw" /I"%~dp0..\delirium-physics\crates\delirium-sdk\include" /I"%~dp0..\..\tools\minhook\include" "%~dp0Adapter.cpp" "%~dp0..\..\tools\minhook\src\hook.c" "%~dp0..\..\tools\minhook\src\buffer.c" "%~dp0..\..\tools\minhook\src\trampoline.c" "%~dp0..\..\tools\minhook\src\hde\hde32.c" "%~dp0..\..\tools\cargo-target\i686-pc-windows-msvc\release\delirium_physics.dll.lib" /link user32.lib advapi32.lib /OUT:DeliriumSA.asi
set build_result=%errorlevel%
popd
exit /b %build_result%

