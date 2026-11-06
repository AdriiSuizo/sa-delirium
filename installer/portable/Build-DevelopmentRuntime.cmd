@echo off
setlocal
call "%~2" -arch=x86 -host_arch=x64 > "%~1\compiler-environment.log" 2>&1
if errorlevel 1 exit /b 2
pushd "%~1"
cl /nologo /LD /MT /O2 /EHsc /std:c++20 /D_CRT_SECURE_NO_WARNINGS /I"%~3\vendor\third-party\minhook\include" /I"%~3\vendor\src\delirium-physics\crates\delirium-sdk\include" /I"%~dp0include" /I"%~dp0include\plugin-sdk-sa\plugin_sa\game_sa\rw" Adapter.cpp "%~3\vendor\third-party\minhook\src\hook.c" "%~3\vendor\third-party\minhook\src\buffer.c" "%~3\vendor\third-party\minhook\src\trampoline.c" "%~3\vendor\third-party\minhook\src\hde\hde32.c" "%~5" /link user32.lib advapi32.lib delayimp.lib /DELAYLOAD:sa_delirium_bootstrap.dll /OUT:DeliriumSA.asi /Brepro
set result=%errorlevel%
popd
exit /b %result%
