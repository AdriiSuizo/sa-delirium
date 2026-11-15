param([Parameter(Mandatory)][string]$Scratch)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'StartupEvidence.ps1')
$root=Join-Path $Scratch ('startup-evidence-'+[Guid]::NewGuid().ToString('N'))
$old=Join-Path $root 'old-p123'
$new=Join-Path $root 'new-p123'
New-Item -ItemType Directory -Path $old,$new -Force | Out-Null
Set-Content -LiteralPath (Join-Path $old 'events.jsonl') -Value '{"pid":123,"event":"initialized"}'
Set-Content -LiteralPath (Join-Path $new 'events.jsonl') -Value '{"pid":999,"event":"initialized"}'
function Check([string]$Expected) {
    $actual=Get-StartupEvidence -Sessions $root -ProcessId 123 -ExistingSessions @('old-p123')
    if($actual.state -ne $Expected){throw "Expected $Expected; got $($actual.state)"}
}
Check 'pending' # Neither recycled PID nor a different live process is proof.
Add-Content -LiteralPath (Join-Path $new 'events.jsonl') -Value '{"pid":123,"event":"initialized"}'
Check 'initialized'
Add-Content -LiteralPath (Join-Path $new 'events.jsonl') -Value '{"pid":123,"event":"native_idle_pose_unavailable"}'
Check 'initialized' # A documented pose fallback is not a failed initialization.
Add-Content -LiteralPath (Join-Path $new 'events.jsonl') -Value '{"pid":123,"event":"engine_error"}'
Check 'startup_error' # Errors after initialization must invalidate the smoke.
Set-Content -LiteralPath (Join-Path $new 'events.jsonl') -Value '{"pid":123,"event":"hook_enable_failed"}'
Check 'startup_error'
Set-Content -LiteralPath (Join-Path $new 'events.jsonl') -Value '{"pid":123,"event":"initialized"}', '{"pid":123,"eve'
Check 'initialized' # A partially-written final line is retried on the next poll.
'PASS 6 startup-evidence regressions'
