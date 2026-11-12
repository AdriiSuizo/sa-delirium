param(
    [Parameter(Mandatory)][string]$Installed,
    [Parameter(Mandatory)][string]$Label,
    [int[]]$Modes=@(291,292,295,296),
    [switch]$RequirePhysical
)
$ErrorActionPreference='Stop'
$tools=Join-Path $Installed 'tools'
. (Join-Path $tools 'Test-Harness.ps1')
$run=Join-Path $tools 'Run-Case.ps1'
$command=Join-Path $tools 'Send-NativeCommand.ps1'
$profile=Join-Path $Installed "logs/$Label-cost.jsonl"
$env:GTASA_FRAME_PROFILE=$profile
$env:GTASA_FRAME_PROFILE_ALL_INTERVALS='1'
$env:GTASA_FRAME_PROFILE_CYCLES='1'
& $run -Action Start -Name $Label | Out-Null
$case=Get-Content (Join-Path $Installed 'logs/workflow-state.json') -Raw | ConvertFrom-Json
try {
    $snapshot=Join-Path $Installed 'logs/bridge/state.json'
    $deadline=(Get-Date).AddSeconds(25)
    do {
        Start-Sleep -Milliseconds 400
        $bridge=if(Test-Path $snapshot){Get-Content $snapshot -Raw | ConvertFrom-Json}
    }while((!$bridge -or $bridge.pid -ne $case.pid) -and (Get-Date) -lt $deadline)
    if(!$bridge -or $bridge.pid -ne $case.pid){throw 'Bridge did not start'}
    $state=Wait-ProjectFreeRoam -Case $case -Timeout 35 -AllowAmbientPhone
    if($state.current_area -eq 3){
        & $command -Action probe_house_transition | Out-Null
        Wait-ProjectFreeRoam -Case $case -Area 0 -Timeout 25 -AllowAmbientPhone | Out-Null
    }
    $scene=& $command -Action probe_grove_street
    if($scene.result -ne 'grove_scene_result_0'){throw $scene.result}
    Start-Sleep -Seconds 3
    $events=Join-Path $Installed 'SA/DeliriumSA-local-v3/events.jsonl'
    foreach($mode in $Modes){
        $start=& $command -Action probe_vehicle_reaction -Mode $mode
        if($start.result -notmatch '^vehicle_reaction_result_(\d+)$'){throw "Setup $mode returned $($start.result)"}
        $ped=[int]$Matches[1]
        $deadline=(Get-Date).AddSeconds(15)
        do {
            Start-Sleep -Milliseconds 400
            $after=& $command -Action probe_vehicle_reaction -Mode 293
        } while($after.result -in 'vehicle_reaction_result_-294','vehicle_reaction_result_-295' -and (Get-Date) -lt $deadline)
        if($after.result -notmatch '^vehicle_reaction_result_(\d+)$'){throw "Result $mode returned $($after.result)"}
        $stale=[int]$Matches[1]
        $rows=Get-Content -LiteralPath $events -Tail 8000 | Where-Object {
            $_ -match '"event":"(probe_road_prop_[^"]+|native_damage_decision|native_road_[^"]+|road_solved_reaction|damage_activation|impact_native_death_[^"]+)"'
        } | ForEach-Object {ConvertFrom-Json $_} | Where-Object pid -eq $case.pid
        $startRow=$rows | Where-Object event -eq 'probe_road_prop_cached' | Select-Object -Last 1
        $changed=$rows | Where-Object { $_.event -eq 'probe_road_prop_motion_started' -and $_.ref -eq $startRow.ref } |
            Select-Object -Last 1
        $damage=$rows | Where-Object {
            $_.event -eq 'native_damage_decision' -and $_.ref -eq $ped -and
            $_.time_ms -ge $startRow.time_ms -and $_.detail -match 'vehicle=\d+ weapon=(49|50)\b'
        } | Select-Object -First 1
        if(!$changed -or !$damage -or $damage.time_ms -gt $changed.time_ms){
            throw "The pedestrian was not hit before the prop in $mode"
        }
        $pedRows=@($rows | Where-Object {$_.ref -eq $ped -and $_.time_ms -ge $startRow.time_ms})
        $pedRows | ConvertTo-Json | Set-Content (Join-Path $case.directory "ped-$mode.json")
        $replaced=$pedRows | Where-Object event -eq 'native_road_damage_animation_replaced' | Select-Object -First 1
        $reaction=$pedRows | Where-Object event -eq 'road_solved_reaction' | Select-Object -First 1
        if($RequirePhysical -and (!$replaced -or !$reaction)){
            throw "Missing native animation replacement or solved body reaction in $mode"
        }
        $summary=[ordered]@{
            model=$startRow.model; initial_mps=(@{291=15;292=15;295=35;296=45}[$mode]);
            placement=$startRow.detail; stale_frames=$stale;
            setup_ms=$startRow.time_ms; pedestrian_hit_ms=$damage.time_ms; prop_moved_ms=$changed.time_ms;
            native_animation=$replaced.detail; physical_reaction=$reaction.detail
        } | ConvertTo-Json -Compress
        Add-Content -LiteralPath (Join-Path $case.directory 'road-props.jsonl') -Value $summary
        $summary
    }
}finally{& $run -Action Finish -Stop | Out-Null}
