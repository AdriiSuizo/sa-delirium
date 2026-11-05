param([Parameter(Mandatory=$true)][string]$Candidate)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$candidateRoot=[IO.Path]::GetFullPath($Candidate)
$manifest=Join-Path $candidateRoot 'portable-manifest.json'
$spec=Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json
if(!$spec.lab_only){throw 'This build script ONLY accepts private validation manifests'}
if(!$spec.tools_sha256){throw 'Missing pinned fixture bundle hash'}
$vswhere=Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'Visual Studio C++ tools required to build the current launcher'}
& (Join-Path $repo 'installer/Build-Launcher.cmd') $candidateRoot (Join-Path $vs 'Common7/Tools/VsDevCmd.bat')
if($LASTEXITCODE -ne 0){throw 'Launcher compile failed'}
$launcher=Join-Path $candidateRoot 'SteamLauncher.exe'
$out=Join-Path $candidateRoot 'SA-Delirium-LocalVerification.exe'
& 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe' /nologo /target:winexe /platform:x64 /optimize+ /codepage:65001 `
    /r:System.Web.Extensions.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll `
    ('/win32manifest:'+(Join-Path $repo 'installer/setup.manifest')) `
    ('/resource:'+$manifest+',portable-manifest.json') ('/resource:'+$launcher+',SteamLauncher.exe') `
    ('/out:'+$out) (Join-Path $repo 'installer/SetupCore.cs') (Join-Path $PSScriptRoot 'PortableCore.cs') (Join-Path $PSScriptRoot 'Editions.cs') (Join-Path $PSScriptRoot 'Standalone.cs') (Join-Path $PSScriptRoot 'PortableSetup.cs')
if($LASTEXITCODE -ne 0){throw 'Private candidate compile failed'}
Get-FileHash -LiteralPath $out | Select-Object Path,Hash | ConvertTo-Json
