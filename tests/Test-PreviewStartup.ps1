param([Parameter(Mandatory=$true)][string]$Game,[Parameter(Mandatory=$true)][string]$Report)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'StartupEvidence.ps1')
$gameRoot=(Resolve-Path -LiteralPath $Game).Path
$exe=Join-Path $gameRoot 'gta_sa.exe'
if(Get-Process gta_sa,gta-sa -ErrorAction SilentlyContinue){throw 'Another SA process is running'}
if(Test-Path -LiteralPath (Join-Path $gameRoot 'scripts/GameTestBridge.asi')){throw 'Test input bridge must be absent'}
Get-ChildItem Env:GTASA_* | ForEach-Object {Remove-Item -LiteralPath ('Env:'+$_.Name)}
$sessions=Join-Path (Split-Path -Parent $gameRoot) 'logs/manual-sessions'
$before=@(Get-ChildItem -LiteralPath $sessions -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name)
$p=Start-Process -FilePath $exe -WorkingDirectory $gameRoot -WindowStyle Hidden -PassThru
$verdict='timeout';$rows=@();$evidence=$null;$settledUntil=$null;$deadline=(Get-Date).AddSeconds(45)
try {
 while((Get-Date) -lt $deadline){
  Start-Sleep -Milliseconds 400
  if($p.HasExited){$verdict='process_exited';break}
  $evidence=Get-StartupEvidence -Sessions $sessions -ProcessId $p.Id -ExistingSessions $before
  $rows=$evidence.events
  if($evidence.state -eq 'startup_error'){$verdict='startup_error';break}
  if($evidence.state -eq 'initialized'){
   if(!$settledUntil){$settledUntil=(Get-Date).AddSeconds(5)}
   if((Get-Date) -ge $settledUntil){$verdict='initialized';break}
  }
 }
} finally {
 if(!$p.HasExited){
  if((Get-Process -Id $p.Id).Path -ne $exe){throw 'Process identity mismatch'}
  Stop-Process -Id $p.Id -Force
 }
}
$result=[ordered]@{pid=$p.Id;game=$gameRoot;verdict=$verdict;session=$evidence.session;asi=(Get-FileHash -LiteralPath (Join-Path $gameRoot 'scripts/DeliriumSA.asi')).Hash;events=$rows}
$result|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $Report -Encoding UTF8
@{pid=$p.Id;verdict=$verdict;asi=$result.asi}|ConvertTo-Json -Compress
if($verdict -ne 'initialized'){exit 1}
