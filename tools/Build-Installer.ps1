param([string]$OutputDirectory, [string]$DevelopmentGame)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$source = Join-Path $root 'installer'
if (!$DevelopmentGame) { $DevelopmentGame = Join-Path (Split-Path -Parent (Split-Path -Parent $root)) 'game' }
$index = Get-Content -LiteralPath (Join-Path $source 'profile-index.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($name in @('scripts/DeliriumSA.asi', 'delirium_physics.dll', 'DeliriumSA.ini')) {
    $entry = $index.files | Where-Object { $_.path -eq $name }
    if (!$entry -or (Get-FileHash -LiteralPath (Join-Path $DevelopmentGame $name)).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw ('Development core does not match the frozen profile: ' + $name)
    }
}
if (!$OutputDirectory) { $OutputDirectory = Join-Path $root 'build/development-installer' }
$build = New-Item -ItemType Directory -Path $OutputDirectory -Force
$vswhere = Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ x86 tools are required to build the launcher' }
$dev = Join-Path $vs 'Common7/Tools/VsDevCmd.bat'
& (Join-Path $source 'Build-Launcher.cmd') $build.FullName $dev
if ($LASTEXITCODE -ne 0) { throw 'Launcher compile failed' }
$csc = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$exe = Join-Path $build.FullName 'SA-Delirium-Setup.exe'
& $csc /nologo /target:winexe /platform:anycpu /optimize+ /utf8output /codepage:65001 `
    ('/win32manifest:' + (Join-Path $source 'setup.manifest')) `
    /reference:System.dll /reference:System.Core.dll /reference:System.Drawing.dll `
    /reference:System.Windows.Forms.dll /reference:System.Web.Extensions.dll `
    ('/resource:' + (Join-Path $source 'profile-index.json') + ',profile-index.json') `
    ('/resource:' + (Join-Path $build.FullName 'SteamLauncher.exe') + ',SteamLauncher.exe') `
    ('/resource:' + (Join-Path $DevelopmentGame 'scripts/DeliriumSA.asi') + ',core.DeliriumSA.asi') `
    ('/resource:' + (Join-Path $DevelopmentGame 'delirium_physics.dll') + ',core.delirium_physics.dll') `
    ('/resource:' + (Join-Path $DevelopmentGame 'DeliriumSA.ini') + ',core.DeliriumSA.ini') `
    ('/out:' + $exe) (Join-Path $source 'SetupCore.cs') (Join-Path $source 'Setup.cs')
if ($LASTEXITCODE -ne 0) { throw 'Installer compile failed' }
Get-FileHash -LiteralPath $exe | Select-Object Path,Hash | ConvertTo-Json
