param(
    [Parameter(Mandatory=$true)][string]$Version,
    [string]$LoaderArchive,
    [string]$Rar = (Join-Path $env:ProgramFiles 'WinRAR/Rar.exe')
)
$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+(?:-[a-z0-9.]+)?$') { throw 'Invalid version' }
$root = Split-Path -Parent $PSScriptRoot
$dist = New-Item -ItemType Directory -Path (Join-Path $root 'dist') -Force
if (!(Test-Path -LiteralPath $Rar -PathType Leaf)) { throw 'WinRAR Rar.exe is required to create a RAR archive' }
$rarExe = (Resolve-Path -LiteralPath $Rar).Path
$stage = New-Item -ItemType Directory -Path (Join-Path $root ('build/' + [Guid]::NewGuid().ToString('N')))
$runtime = New-Item -ItemType Directory -Path (Join-Path $stage.FullName 'payload')
$scripts = New-Item -ItemType Directory -Path (Join-Path $runtime.FullName 'scripts')
$licenses = New-Item -ItemType Directory -Path (Join-Path $runtime.FullName 'SAStandalone-licenses')
if (!$LoaderArchive) {
    $LoaderArchive = Join-Path $stage.FullName 'loader.zip'
    Invoke-WebRequest -Uri 'https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v10.0.2/Ultimate-ASI-Loader.zip' -OutFile $LoaderArchive
}
if ((Get-FileHash -LiteralPath $LoaderArchive -Algorithm SHA256).Hash -ne 'A8F76A891F9BEEEF3B02BABF1062C128B00E0285FA17C227A45EC4F81F45116F') { throw 'Pinned loader archive hash mismatch' }
$dependency = Join-Path $stage.FullName 'loader'
Expand-Archive -LiteralPath $LoaderArchive -DestinationPath $dependency
$loader = @(Get-ChildItem -LiteralPath $dependency -Recurse -File -Filter 'dinput8.dll')
if ($loader.Count -ne 1) { throw 'Unexpected loader archive layout' }
$vswhere = Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs -or $LASTEXITCODE -ne 0) { throw 'Install Visual Studio Build Tools with the x86 C++ toolset' }
$dev = Join-Path $vs 'Common7/Tools/VsDevCmd.bat'
& (Join-Path $PSScriptRoot 'Compile.cmd') $stage.FullName $dev
if ($LASTEXITCODE -ne 0) { throw 'Win32 compile failed' }
Copy-Item -LiteralPath $loader[0].FullName -Destination (Join-Path $runtime.FullName 'dinput8.dll')
Copy-Item -LiteralPath (Join-Path $stage.FullName 'SAStandalone.asi') -Destination $scripts.FullName
Copy-Item -LiteralPath (Join-Path $root 'package/scripts/SAStandalone.ini') -Destination $scripts.FullName
Copy-Item -LiteralPath (Join-Path $root 'docs/INSTALL.md') -Destination (Join-Path $runtime.FullName 'SAStandalone-LEEME.md')
Copy-Item -LiteralPath (Join-Path $root 'CREDITS.md') -Destination (Join-Path $runtime.FullName 'SAStandalone-CREDITOS.md')
Copy-Item -LiteralPath (Join-Path $root 'licenses/Ultimate-ASI-Loader.txt') -Destination $licenses.FullName
Copy-Item -LiteralPath (Join-Path $root 'LICENSE.md') -Destination $licenses.FullName
$expected = @(
    'dinput8.dll','scripts/SAStandalone.asi','scripts/SAStandalone.ini',
    'SAStandalone-LEEME.md','SAStandalone-CREDITOS.md',
    'SAStandalone-licenses/Ultimate-ASI-Loader.txt','SAStandalone-licenses/LICENSE.md'
)
$actual = @(Get-ChildItem -LiteralPath $runtime.FullName -Recurse -File | ForEach-Object {
    $_.FullName.Substring($runtime.FullName.Length + 1).Replace('\','/')
})
if (Compare-Object ($expected | Sort-Object) ($actual | Sort-Object)) { throw 'Payload file allowlist mismatch' }
$archive = Join-Path $dist.FullName "SA-Standalone-$Version.rar"
if (Test-Path -LiteralPath $archive) { throw "Archive already exists. Choose a new version or move the old artifact: $archive" }
Push-Location -LiteralPath $runtime.FullName
try {
    & $rarExe a -ma5 -m5 -idq $archive @expected
    if ($LASTEXITCODE -ne 0) { throw 'RAR creation failed' }
    & $rarExe t -idq $archive
    if ($LASTEXITCODE -ne 0) { throw 'RAR integrity check failed' }
    $packed = @(& $rarExe lb $archive | ForEach-Object { $_.Replace('\','/') })
    if ($LASTEXITCODE -ne 0 -or (Compare-Object ($expected | Sort-Object) ($packed | Sort-Object))) {
        throw 'RAR file paths differ from the approved payload'
    }
} finally { Pop-Location }
Get-FileHash -LiteralPath $archive | ConvertTo-Json | Set-Content -LiteralPath "$archive.sha256.json"
Write-Output $archive
