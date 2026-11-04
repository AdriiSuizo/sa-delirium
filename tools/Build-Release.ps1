param(
    [Parameter(Mandatory=$true)][string]$Version,
    [string]$LoaderArchive,
    [string]$SilentPatchArchive,
    [string]$VisualPayload,
    [switch]$LocalPersonalContent,
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
if ($LocalPersonalContent -and !$VisualPayload) { throw 'LocalPersonalContent requires an explicit visual payload' }
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
& python (Join-Path $PSScriptRoot 'Verify-Vendor.py')
if ($LASTEXITCODE -ne 0) { throw 'Retained adapted source verification failed' }
& python (Join-Path $PSScriptRoot 'Collect-RustLicenses.py')
if ($LASTEXITCODE -ne 0) { throw 'Dependency notices are incomplete' }
$targetDir = Join-Path $root 'build/bootstrap-target'
$manifest = Join-Path $root 'bootstrap/Cargo.toml'
Push-Location -LiteralPath $root
try {
    & cargo build --manifest-path $manifest --lib --locked --release --target i686-pc-windows-msvc --target-dir $targetDir
    if ($LASTEXITCODE -ne 0) { throw 'Win32 Delirium bootstrap build failed' }
} finally { Pop-Location }
$bootstrap = Join-Path $targetDir 'i686-pc-windows-msvc/release/sa_delirium_bootstrap.dll'
& python (Join-Path $PSScriptRoot 'Verify-Runtime.py') $bootstrap
if ($LASTEXITCODE -ne 0) { throw 'Runtime needs unbundled libraries' }
$nativeResult = Join-Path $stage.FullName 'native-result.json'
& (Join-Path $root 'native/Build-Adapter.ps1') -BootstrapImportLibrary ($bootstrap + '.lib') -ResultPath $nativeResult *> (Join-Path $stage.FullName 'native-build.log')
if (!$? -or $LASTEXITCODE -ne 0) { throw 'Native adapter build failed; see native-build.log' }
$nativeBuild = Get-Content -LiteralPath $nativeResult -Raw | ConvertFrom-Json
if ($nativeBuild.unresolved -ne 0 -or $nativeBuild.status -ne 'linked_not_installed_or_gameplay_accepted') { throw 'Native adapter bindings are incomplete' }
if (!$SilentPatchArchive) {
    $SilentPatchArchive = Join-Path $stage.FullName 'SilentPatchSA.zip'
    Invoke-WebRequest -Uri 'https://github.com/CookiePLMonster/SilentPatch/releases/download/1.1-BUILD34.1-SA/SilentPatchSA.zip' -OutFile $SilentPatchArchive
}
if ((Get-FileHash -LiteralPath $SilentPatchArchive -Algorithm SHA256).Hash -ne '0837BBDC0548A4A952F924ADEFD2BB6EACC866407F153844FE1F56995668C42C') { throw 'Pinned SilentPatch archive hash mismatch' }
$silent = Join-Path $stage.FullName 'silentpatch'
Expand-Archive -LiteralPath $SilentPatchArchive -DestinationPath $silent
Copy-Item -LiteralPath $loader[0].FullName -Destination (Join-Path $runtime.FullName 'dinput8.dll')
Copy-Item -LiteralPath (Join-Path $stage.FullName 'SAStandalone.asi') -Destination $scripts.FullName
Copy-Item -LiteralPath $bootstrap -Destination $scripts.FullName
Copy-Item -LiteralPath (Join-Path $nativeBuild.output 'DeliriumSA.asi') -Destination $scripts.FullName
Copy-Item -LiteralPath (Join-Path $root 'package/DeliriumSA.ini') -Destination $runtime.FullName
Copy-Item -LiteralPath (Join-Path $silent 'SilentPatchSA.asi') -Destination $scripts.FullName
Copy-Item -LiteralPath (Join-Path $silent 'SilentPatchSA.ini') -Destination $scripts.FullName
Copy-Item -LiteralPath (Join-Path $root 'package/scripts/SAStandalone.ini') -Destination $scripts.FullName
Copy-Item -LiteralPath (Join-Path $root 'docs/INSTALL.md') -Destination (Join-Path $runtime.FullName 'SAStandalone-LEEME.md')
Copy-Item -LiteralPath (Join-Path $root 'CREDITS.md') -Destination (Join-Path $runtime.FullName 'SAStandalone-CREDITOS.md')
Copy-Item -LiteralPath (Join-Path $root 'licenses/Ultimate-ASI-Loader.txt') -Destination $licenses.FullName
Copy-Item -LiteralPath (Join-Path $root 'LICENSE.md') -Destination $licenses.FullName
Copy-Item -LiteralPath (Join-Path $root 'bootstrap/COPYING') -Destination (Join-Path $licenses.FullName 'Delirium-GPL-3.0.txt')
foreach ($notice in 'Xenia-BSD-3-Clause.txt','SilentPatch-MIT.txt','Rust-Dependencies.txt') { Copy-Item -LiteralPath (Join-Path $root ('licenses/' + $notice)) -Destination $licenses.FullName }
Copy-Item -LiteralPath (Join-Path $root 'vendor/third-party/minhook/LICENSE.txt') -Destination (Join-Path $licenses.FullName 'MinHook-BSD.txt')
foreach ($binary in 'DeliriumSA.asi','SilentPatchSA.asi','SAStandalone.asi') {
    & python (Join-Path $PSScriptRoot 'Verify-Runtime.py') (Join-Path $scripts.FullName $binary) --bundled sa_delirium_bootstrap.dll
    if ($LASTEXITCODE -ne 0) { throw ('Unbundled dependency in ' + $binary) }
}
$expected = @(
    'dinput8.dll','scripts/SAStandalone.asi','scripts/SAStandalone.ini','scripts/sa_delirium_bootstrap.dll',
    'SAStandalone-LEEME.md','SAStandalone-CREDITOS.md',
    'SAStandalone-licenses/Ultimate-ASI-Loader.txt','SAStandalone-licenses/LICENSE.md',
    'SAStandalone-licenses/Delirium-GPL-3.0.txt',
    'scripts/DeliriumSA.asi','DeliriumSA.ini','scripts/SilentPatchSA.asi','scripts/SilentPatchSA.ini',
    'SAStandalone-licenses/Xenia-BSD-3-Clause.txt','SAStandalone-licenses/SilentPatch-MIT.txt','SAStandalone-licenses/MinHook-BSD.txt','SAStandalone-licenses/Rust-Dependencies.txt'
)
if ($VisualPayload) {
    $visualRoot = (Resolve-Path -LiteralPath $VisualPayload).Path
    & python (Join-Path $PSScriptRoot 'Verify-VisualPayload.py') $visualRoot
    if ($LASTEXITCODE -ne 0) { throw 'Visual payload is invalid' }
    $visualManifest = Get-Content -LiteralPath (Join-Path $visualRoot 'manifest.json') -Raw | ConvertFrom-Json
    if ($visualManifest.redistributable -ne $true -and !$LocalPersonalContent) {
        throw 'This payload is local-only. It cannot be included in a distributable release.'
    }
    if ($LocalPersonalContent -and $Version -notmatch 'local') { throw 'A personal-content archive must have local in its version' }
    Copy-Item -LiteralPath $visualRoot -Destination (Join-Path $runtime.FullName 'SAStandalone-assets') -Recurse
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $runtime.FullName 'SAStandalone-assets') -Recurse -File) {
        $expected += $file.FullName.Substring($runtime.FullName.Length + 1).Replace('\','/')
    }
    if ($LocalPersonalContent) {
        Copy-Item -LiteralPath (Join-Path $root 'docs/LOCAL-CONTENT.md') -Destination (Join-Path $runtime.FullName 'LOCAL-CONTENT.md')
        $expected += 'LOCAL-CONTENT.md'
    }
}
$sourceCommit = 'unversioned-source'
if (Test-Path -LiteralPath (Join-Path $root '.git')) { $sourceCommit = (& git -C $root rev-parse HEAD) }
$content = [ordered]@{
    schema = 1
    version = $Version
    profile = $(if ($LocalPersonalContent) { 'local_personal_candidate_not_for_upload' } else { 'redistributable_core_candidate' })
    source_commit = $sourceCommit
    completeness = 'scoped_candidate_not_complete_requested_modpack'
    included = @('Native Delirium SA adapter and shared ISO importer','Ultimate ASI Loader 10.0.2','SilentPatch SA 34.1',
        'Own PS2 colour/time cycle and native post-effect configuration','Own borderless/right-stick/suspension integration','Own owner-backed streaming budget and asset composer')
    excluded = @('GTA V HUD/minimap/weapon wheel','DLSS','test probes','game executables','ISO/cache/save files','development tools')
    native_frame_limit_preserved = $true
}
if ($VisualPayload) {
    $content.visual_manifest_sha256 = (Get-FileHash -LiteralPath (Join-Path $visualRoot 'manifest.json')).Hash.ToLowerInvariant()
    $content.visual_sources = (Get-Content -LiteralPath (Join-Path $visualRoot 'provenance.json') -Raw | ConvertFrom-Json).sources
}
$content | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runtime.FullName 'SAStandalone-CONTENTS.json') -Encoding UTF8
$expected += 'SAStandalone-CONTENTS.json'
$actual = @(Get-ChildItem -LiteralPath $runtime.FullName -Recurse -File | ForEach-Object {
    $_.FullName.Substring($runtime.FullName.Length + 1).Replace('\','/')
})
if (Compare-Object ($expected | Sort-Object) ($actual | Sort-Object)) { throw 'Payload file allowlist mismatch' }
$archive = Join-Path $dist.FullName "SA-Standalone-$Version.rar"
if (Test-Path -LiteralPath $archive) { throw "Archive already exists. Choose a new version or move the old artifact: $archive" }
Push-Location -LiteralPath $runtime.FullName
try {
    # A visual payload exceeds Windows' process command-line limit. RAR reads
    # this UTF-16 list, preserving spaces/non-ASCII names without shell quoting.
    $list = Join-Path $stage.FullName 'payload-files.txt'
    $expected | ForEach-Object { '"' + $_ + '"' } | Set-Content -LiteralPath $list -Encoding Unicode
    & $rarExe a -ma5 -m5 -idq -scul $archive ('@' + $list)
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
