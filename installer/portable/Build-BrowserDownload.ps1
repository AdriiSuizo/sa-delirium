param([Parameter(Mandatory=$true)][string]$Out)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$sdk = Join-Path $repo 'build/webview2-sdk'
$destination = [IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$core = Join-Path $sdk 'lib/net462/Microsoft.Web.WebView2.Core.dll'
$forms = Join-Path $sdk 'lib/net462/Microsoft.Web.WebView2.WinForms.dll'
& 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe' /nologo /target:winexe /platform:x64 /codepage:65001 "/out:$destination\BrowserDownload.exe" /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Web.Extensions.dll "/r:$core" "/r:$forms" (Join-Path $PSScriptRoot 'BrowserDownload.cs')
if ($LASTEXITCODE -ne 0) { throw 'Browser helper compile failed' }
Copy-Item -LiteralPath $core,$forms -Destination $destination
Copy-Item -LiteralPath (Join-Path $sdk 'runtimes/win-x64/native/WebView2Loader.dll') -Destination $destination
Copy-Item -LiteralPath (Join-Path $sdk 'LICENSE.txt') -Destination (Join-Path $destination 'WebView2-LICENSE.txt')
