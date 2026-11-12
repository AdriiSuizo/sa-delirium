param([Parameter(Mandatory)][string]$Installed)
$ErrorActionPreference='Stop'
$tools=Join-Path $Installed 'tools'
. (Join-Path $tools 'Test-Harness.ps1')
$run=Join-Path $tools 'Run-Case.ps1'
$command=Join-Path $tools 'Send-NativeCommand.ps1'
& $run -Action Start -Name 'cj-high-fall' | Out-Null
$case=Get-Content (Join-Path $Installed 'logs/workflow-state.json') -Raw | ConvertFrom-Json
try{
    $loaded=Wait-ProjectFreeRoam -Case $case -Timeout 40 -AllowAmbientPhone
    if($loaded.current_area -eq 3){
        & $command -Action probe_house_transition | Out-Null
        Wait-ProjectFreeRoam -Case $case -Area 0 -Timeout 25 -AllowAmbientPhone | Out-Null
    }
    $scene=& $command -Action probe_grove_street
    if($scene.result -ne 'grove_scene_result_0'){throw $scene.result}
    Start-Sleep -Seconds 2
    $drop=& $command -Action probe_vehicle_reaction -Mode 340
    if($drop.result -notmatch '^vehicle_reaction_result_(\d+)$'){throw $drop.result}
    $playerRef=[int]$Matches[1]
    Start-Sleep -Milliseconds 250
    $frame=& $command -Action probe_visual_frame
    if($frame.result -eq 'visual_frame_ok'){
        Copy-Item (Join-Path $Installed "logs/bridge/visual-frame-$($case.pid).bmp") (Join-Path $case.directory 'fall.bmp')
    }
    Start-Sleep -Seconds 8
    $log=Join-Path $Installed 'SA/DeliriumSA-local-v3/events.jsonl'
    $rows=@(Get-Content -LiteralPath $log -Tail 9000 |
        Where-Object {$_ -match 'player_high_fall|cj_flight_voice|probe_cj_high_fall|weapon=54\b|recovery_finished|native_getup|damage_activation|physics_released|engine_error'} |
        ConvertFrom-Json | Where-Object {$_.pid -eq $case.pid -and $_.ref -eq $playerRef})
    $rows | ConvertTo-Json | Set-Content (Join-Path $case.directory 'fall.json')
    $rows | Where-Object event -in 'player_high_fall_started','player_high_fall_landed','cj_flight_voice','native_damage_decision','engine_error' |
        Select-Object event,detail | ConvertTo-Json -Compress
    if(!($rows|Where-Object event -eq 'player_high_fall_started') -or
       !($rows|Where-Object event -eq 'player_high_fall_landed') -or
       !($rows|Where-Object event -eq 'native_getup_completed') -or
       !($rows|Where-Object {$_.event -eq 'cj_flight_voice' -and $_.detail -match 'speech=\d+'}) -or
       !($rows|Where-Object {$_.event -eq 'native_damage_decision' -and $_.detail -match 'weapon=54\b'})){
        throw 'Missing high-fall task, landing, CJ voice, native damage or get-up'
    }
}finally {& $run -Action Finish -Stop | Out-Null}