param(
    [Parameter(Mandatory=$true)][string]$Package,
    [ValidateSet('x64','x86')][string]$Architecture='x64'
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$packageRoot=[IO.Path]::GetFullPath($Package)
$manifest=Join-Path $packageRoot 'portable-manifest.json'
$spec=Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json
if($spec.lab_only){throw 'Private verification payloads are not accepted by the distribution builder'}
$routing=Get-Content -LiteralPath (Join-Path $packageRoot 'routing-report.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if($routing.unresolved.Count -ne 0){throw 'Unaccounted distribution inputs remain'}
$bundle=Join-Path $packageRoot 'portable-tools.zip'
if((Get-FileHash -LiteralPath $bundle -Algorithm SHA256).Hash.ToLowerInvariant() -ne $spec.tools_sha256){throw 'Tools bundle fingerprint changed'}
if($Architecture -eq 'x86') {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip=[IO.Compression.ZipFile]::OpenRead($bundle)
    try {
        foreach($entry in $zip.Entries) {
            if([IO.Path]::GetExtension($entry.FullName) -notin @('.exe','.dll','.asi','.cleo')){continue}
            $stream=$entry.Open()
            $bytes=New-Object IO.MemoryStream
            try {
                $stream.CopyTo($bytes)
                $data=$bytes.ToArray()
                if($data.Length -lt 64 -or $data[0] -ne 77 -or $data[1] -ne 90){throw "Invalid PE file: $($entry.FullName)"}
                $offset=[BitConverter]::ToUInt32($data,60)
                if($offset -gt $data.Length-24 -or [BitConverter]::ToUInt32($data,$offset) -ne 17744 -or [BitConverter]::ToUInt16($data,$offset+4) -ne 332){
                    throw "An x86 installer requires x86 executables and libraries: $($entry.FullName)"
                }
            } finally {$stream.Dispose();$bytes.Dispose()}
        }
    } finally {$zip.Dispose()}
}
$vswhere=Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'C++ build tools required'}
& (Join-Path $repo 'installer/Build-Launcher.cmd') $packageRoot (Join-Path $vs 'Common7/Tools/VsDevCmd.bat')
if($LASTEXITCODE -ne 0){throw 'Current Steam launcher build failed'}
$launcher=Join-Path $packageRoot 'SteamLauncher.exe'
$version=if($spec.version){$spec.version}else{'0.3.0'}
$out=Join-Path $packageRoot ('SA-Delirium-'+$version+'-Setup.exe')
& 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe' /nologo /target:winexe ("/platform:"+$Architecture) /optimize+ /codepage:65001 `
    /r:System.Web.Extensions.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll `
    ('/win32manifest:'+(Join-Path $repo 'installer/setup.manifest')) `
    ('/resource:'+$manifest+',portable-manifest.json') ('/resource:'+$launcher+',SteamLauncher.exe') ('/resource:'+$bundle+',portable-tools.zip') `
    ('/out:'+$out) (Join-Path $repo 'installer/SetupCore.cs') (Join-Path $PSScriptRoot 'PortableCore.cs') (Join-Path $PSScriptRoot 'Editions.cs') (Join-Path $PSScriptRoot 'Standalone.cs') (Join-Path $PSScriptRoot 'PortableSetup.cs')
if($LASTEXITCODE -ne 0){throw 'Portable installer compile failed'}
Get-FileHash -LiteralPath $out | Select-Object Path,Hash | ConvertTo-Json
