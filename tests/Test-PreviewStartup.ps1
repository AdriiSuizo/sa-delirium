param([Parameter(Mandatory=$true)][string]$Game,[Parameter(Mandatory=$true)][string]$Report)
$ErrorActionPreference='Stop'
$gameRoot=(Resolve-Path -LiteralPath $Game).Path
$exe=Join-Path $gameRoot 'gta_sa.exe'
if(Get-Process gta_sa,gta-sa -ErrorAction SilentlyContinue){throw 'Another SA process is running'}
if(Test-Path -LiteralPath (Join-Path $gameRoot 'scripts/GameTestBridge.asi')){throw 'Test input bridge must be absent'}
Get-ChildItem Env:GTASA_* | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
$p=Start-Process -FilePath $exe -WorkingDirectory $gameRoot -WindowStyle Hidden -PassThru
$verdict='timeout';$rows=@();$deadline=(Get-Date).AddSeconds(45)
try {
 while((Get-Date) -lt $deadline){
  Start-Sleep -Milliseconds 400
  if($p.HasExited){$verdict='process_exited';break}
  $log=Join-Path $gameRoot 'DeliriumSA-local-v3/events.jsonl'
  if(!(Test-Path -LiteralPath $log)){continue}
  $rows=@(Get-Content -LiteralPath $log | ForEach-Object {try {$r=$_|ConvertFrom-Json;if($r.pid -eq $p.Id){$r}}catch{}})
  if($rows | Where-Object {$_.event -eq 'initialized'}){
   Start-Sleep -Seconds 5
   $verdict=if($p.HasExited){'exited_after_init'}else{'initialized'}
   break
  }
 }
} finally {
 if(!$p.HasExited){
  if((Get-Process -Id $p.Id).Path -ne $exe){throw 'Process identity mismatch'}
  Stop-Process -Id $p.Id -Force
 }
}
$result=[ordered]@{pid=$p.Id;game=$gameRoot;verdict=$verdict;asi=(Get-FileHash -LiteralPath (Join-Path $gameRoot 'scripts/DeliriumSA.asi')).Hash;events=$rows}
$result|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $Report -Encoding UTF8
@{pid=$p.Id;verdict=$verdict;asi=$result.asi}|ConvertTo-Json -Compress
if($verdict -ne 'initialized'){exit 1}
