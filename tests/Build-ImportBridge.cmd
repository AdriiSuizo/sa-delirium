@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -host_arch=x64 -arch=x86 >nul
if errorlevel 1 exit /b 2
pushd "%~1"
cl /nologo /LD /MT /EHsc /O2 /std:c++20 /DGTASA /D_CRT_SECURE_NO_WARNINGS /I"%~2\src\sa-test-bridge" /I"%~2\tools\minhook\include" AgentBridge.cpp "%~2\tools\minhook\src\hook.c" "%~2\tools\minhook\src\buffer.c" "%~2\tools\minhook\src\trampoline.c" "%~2\tools\minhook\src\hde\hde32.c" /link user32.lib gdi32.lib /OUT:GameTestBridge.asi
set result=%errorlevel%
popd
exit /b %result%
