param([Parameter(Mandatory)][string]$Installed)
$ErrorActionPreference='Stop'
$tools=Join-Path $Installed 'tools'
. (Join-Path $tools 'Test-Harness.ps1')
$run=Join-Path $tools 'Run-Case.ps1'
$command=Join-Path $tools 'Send-NativeCommand.ps1'
$events=Join-Path $Installed 'SA/DeliriumSA-local-v3/events.jsonl'
function Fixture([int]$Mode) {
    $reply=& $command -Action probe_vehicle_reaction -Mode $Mode
    if($reply.result -notmatch '^vehicle_reaction_result_(\d+)$'){throw "$Mode returned $($reply.result)"}
    [int]$Matches[1]
}

$priorAudit=$env:GTASA_WRIST_AUDIT
$env:GTASA_WRIST_AUDIT='1'
try {
    & $run -Action Start -Name 'wrist-melee-audio' | Out-Null
    $case=Get-Content (Join-Path $Installed 'logs/workflow-state.json') -Raw | ConvertFrom-Json
} finally {
    $env:GTASA_WRIST_AUDIT=$priorAudit
}
try {
    $state=Wait-ProjectFreeRoam -Case $case -Timeout 40 -AllowAmbientPhone
    if($state.current_area -eq 3) {
        & $command -Action probe_house_transition | Out-Null
        Wait-ProjectFreeRoam -Case $case -Area 0 -Timeout 25 -AllowAmbientPhone | Out-Null
    }
    $scene=& $command -Action probe_grove_street
    if($scene.result -ne 'grove_scene_result_0'){throw $scene.result}
    Start-Sleep -Seconds 3
    $punch=Fixture 165
    & $command -Action probe_delirium_focus -TargetRef $punch | Out-Null
    Start-Sleep -Milliseconds 800
    Fixture 152 | Out-Null
    Start-Sleep -Seconds 2

    $shot=Fixture 40
    & $command -Action probe_delirium_focus -TargetRef $shot | Out-Null
    Start-Sleep -Milliseconds 400
    Fixture 42 | Out-Null
    Start-Sleep -Milliseconds 750
    & $command -Action probe_visual_frame | Out-Null
    $frame=Join-Path $Installed "logs/bridge/visual-frame-$($case.pid).bmp"
    if(Test-Path $frame){Copy-Item $frame (Join-Path $case.directory 'shot.bmp')}

    $bat=Fixture 40
    & $command -Action probe_delirium_focus -TargetRef $bat | Out-Null
    Start-Sleep -Milliseconds 400
    Fixture 47 | Out-Null
    Start-Sleep -Seconds 2

    $rows=@(Get-Content -LiteralPath $events -Tail 8000 |
        Where-Object {$_ -match '"event":"(physics_wrist_pose|probe_recovery_damage|native_damage_decision|combat_contact|combat_authored_damage|iv_melee_audio|melee_contact_response|recovery_start)"'} |
        ConvertFrom-Json | Where-Object {$_.pid -eq $case.pid -and $_.ref -in $shot,$bat,$punch})
    $rows | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $case.directory 'impact-evidence.json')
    $wrists=@($rows | Where-Object {$_.ref -eq $shot -and $_.event -eq 'physics_wrist_pose' -and $_.detail -match '^reaction=0\b'})
    $angles=@($wrists | ForEach-Object {
        if($_.detail -match 'left_deg=([\d.]+) right_deg=([\d.]+)') {[double]$Matches[1]; [double]$Matches[2]}
    })
    $batSound=@($rows | Where-Object {$_.event -eq 'iv_melee_audio' -and $_.ref -eq $bat -and $_.detail -eq 'bat'})
    $punchSound=@($rows | Where-Object {$_.event -eq 'iv_melee_audio' -and $_.ref -eq $punch -and $_.detail -match '^(fist|face)'})
    [pscustomobject]@{directory=$case.directory;shot=$shot;bat=$bat;punch=$punch;wrist_samples=$wrists.Count;
        max_wrist_deg=($angles | Measure-Object -Maximum).Maximum;bat_sounds=$batSound.Count;punch_sounds=$punchSound.Count} | ConvertTo-Json -Compress
    if(!$angles.Count -or ($angles | Where-Object {$_ -gt 38})) {throw 'Shot did not produce a constrained wrist pose'}
    if($batSound.Count -ne 1 -or $punchSound.Count -lt 1){throw 'Missing or duplicate impact sound'}
} finally {
    & $run -Action Finish -Stop | Out-Null
}