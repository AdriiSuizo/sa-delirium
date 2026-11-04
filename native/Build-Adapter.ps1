param([switch]$CompileOnly,[string]$BootstrapImportLibrary,[string]$ResultPath)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root ('build/native-' + [Guid]::NewGuid().ToString('N'))
& python (Join-Path $PSScriptRoot 'Prepare-Adapter.py') --output $output
if ($LASTEXITCODE -ne 0) { throw 'Source preparation failed' }
$receipt = Get-Content -LiteralPath (Join-Path $output 'preparation.json') -Raw | ConvertFrom-Json
if (!$CompileOnly -and $receipt.status -ne 'prepared_requires_gameplay_validation') {
    throw 'Native bindings are incomplete. CompileOnly can check source dependencies, but cannot build a runnable module.'
}
$vswhere = Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs -or $LASTEXITCODE -ne 0) { throw 'An x86 C++ toolset is required' }
$vc = Join-Path $vs 'Common7/Tools/VsDevCmd.bat'
$include = @((Join-Path $root 'vendor/third-party/minhook/include'), (Join-Path $root 'vendor/src/delirium-physics/crates/delirium-sdk/include'))
$flags = ($include | ForEach-Object { '/I"' + $_ + '"' }) -join ' '
$compile = 'cl /nologo /c /MT /O2 /EHsc /std:c++20 /D_CRT_SECURE_NO_WARNINGS ' + $flags + ' "' + (Join-Path $output 'Adapter.cpp') + '" "' + (Join-Path $output 'HostEntityTransform.cpp') + '"'
$commands = @('@echo off', ('call "' + $vc + '" -host_arch=x64 -arch=x86 >nul'), 'if errorlevel 1 exit /b %errorlevel%', $compile, 'if errorlevel 1 exit /b %errorlevel%')
if (!$CompileOnly) {
    $sdk = $BootstrapImportLibrary
    if (!$sdk) { $sdk = Join-Path $root 'bootstrap/target/i686-pc-windows-msvc/release/sa_delirium_bootstrap.dll.lib' }
    $sdk = (Resolve-Path -LiteralPath $sdk).Path
    if (!(Test-Path -LiteralPath $sdk)) { throw 'Build the shared x86 bootstrap before linking the adapter' }
    $minhook = Join-Path $root 'vendor/third-party/minhook/src'
    $sources = @('hook.c','buffer.c','trampoline.c','hde/hde32.c') | ForEach-Object { '"' + (Join-Path $minhook $_) + '"' }
    $commands += ('cl /nologo /c /TC /MT /O2 ' + $flags + ' ' + ($sources -join ' '))
    $commands += 'if errorlevel 1 exit /b %errorlevel%'
    $commands += ('link /nologo /DLL /MACHINE:X86 /OUT:DeliriumSA.asi Adapter.obj HostEntityTransform.obj hook.obj buffer.obj trampoline.obj hde32.obj "' + $sdk + '" user32.lib advapi32.lib bcrypt.lib')
}
$batch = Join-Path $output 'compile.cmd'
Set-Content -LiteralPath $batch -Encoding utf8 -Value ($commands + 'exit /b %errorlevel%')
Push-Location -LiteralPath $output
try {
    & cmd.exe /d /c $batch *> compile.log
    if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath compile.log -Tail 14; throw 'Native source compilation failed' }
    $receipt | Add-Member -NotePropertyName compilation_passed -NotePropertyValue $true
    $receipt | Add-Member -NotePropertyName compile_only -NotePropertyValue ([bool]$CompileOnly)
    $receipt | Add-Member -NotePropertyName object_sha256 -NotePropertyValue @{
        Adapter = (Get-FileHash -LiteralPath Adapter.obj -Algorithm SHA256).Hash
        HostEntityTransform = (Get-FileHash -LiteralPath HostEntityTransform.obj -Algorithm SHA256).Hash
    }
    $preparedHashes = @{}
    Get-ChildItem -LiteralPath $output -File | Where-Object { $_.Extension -in '.hpp','.cpp','.inc' } | ForEach-Object { $preparedHashes[$_.Name] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    $receipt | Add-Member -NotePropertyName prepared_source_sha256 -NotePropertyValue $preparedHashes
    $receipt | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath compilation.json
    if (!$CompileOnly) {
        $receipt | Add-Member -NotePropertyName adapter_sha256 -NotePropertyValue (Get-FileHash -LiteralPath DeliriumSA.asi -Algorithm SHA256).Hash
        $receipt | Add-Member -NotePropertyName bootstrap_import_library_sha256 -NotePropertyValue (Get-FileHash -LiteralPath $sdk -Algorithm SHA256).Hash
        $receipt | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath linked.json
    }
    $result = @{ status=$(if ($CompileOnly) {'compiled_without_installation'} else {'linked_not_installed_or_gameplay_accepted'}); output=$output; unresolved=@($receipt.unresolved.PSObject.Properties).Count }
    if ($ResultPath) { $result | ConvertTo-Json | Set-Content -LiteralPath $ResultPath }
    $result | ConvertTo-Json
} finally { Pop-Location }
