param([Parameter(Mandatory)][string]$Installed)
$ErrorActionPreference='Stop'
$tools=Join-Path $Installed 'tools'
. (Join-Path $tools 'Test-Harness.ps1')
$run=Join-Path $tools 'Run-Case.ps1'
$command=Join-Path $tools 'Send-NativeCommand.ps1'
$log=Join-Path $Installed 'SA/DeliriumSA-local-v3/events.jsonl'
foreach($mode in 338,339){
    $previous=$env:GTASA_VEHICLE_LAB
    $env:GTASA_VEHICLE_LAB='traffic'
    try {& $run -Action Start -Name "vehicle-entry-$mode" | Out-Null}
    finally {$env:GTASA_VEHICLE_LAB=$previous}
    $case=Get-Content (Join-Path $Installed 'logs/workflow-state.json') -Raw|ConvertFrom-Json
    try{
        $state=Wait-ProjectFreeRoam -Case $case -Timeout 40 -AllowAmbientPhone
        if($state.current_area -eq 3){
            & $command -Action probe_house_transition | Out-Null
            Wait-ProjectFreeRoam -Case $case -Area 0 -Timeout 25 -AllowAmbientPhone | Out-Null
        }
        $scene=& $command -Action probe_grove_street
        if($scene.result -ne 'grove_scene_result_0'){throw $scene.result}
        Start-Sleep -Seconds 2
        $reply=& $command -Action probe_vehicle_reaction -Mode $mode
        if($reply.result -notmatch '^vehicle_reaction_result_(\d+)$'){throw $reply.result}
        $target=[int]$Matches[1]
        Start-Sleep -Seconds 10
        $rows=@(Get-Content -LiteralPath $log -Tail 10000 |
            Where-Object {$_ -match '"event":"vehicle_entry_lab_'} |
            ConvertFrom-Json | Where-Object {$_.pid -eq $case.pid -and $_.ref -eq $target})
        $rows | ConvertTo-Json | Set-Content (Join-Path $case.directory 'entry.json')
        $open=$rows | Where-Object event -eq 'vehicle_entry_lab_opening' | Select-Object -First 1
        $seat=$rows | Where-Object event -eq 'vehicle_entry_lab_seated' | Select-Object -First 1
        $iv=$rows | Where-Object {$_.event -eq 'vehicle_entry_lab_iv' -and $_.detail -eq 'driver_still_seated=1'} | Select-Object -First 1
        $moves=@($rows|Where-Object {$_.event -eq 'vehicle_entry_lab_frame' -and $_.detail -match '^door=1 driver=0'} |
            ForEach-Object {if($_.detail -match 'move_m=([\d.]+)'){[double]$Matches[1]}})
        [ordered]@{case=$case.directory;model=$rows[0].model;opened=[bool]$open;seated=[bool]$seat;
            iv_delay_ms=if($seat -and $iv){$iv.time_ms-$seat.time_ms};max_opening_step_m=($moves|Measure-Object -Maximum).Maximum} |
            ConvertTo-Json -Compress
        if(!$open -or !$seat -or !$iv -or $iv.time_ms-$seat.time_ms -gt 100 -or
           ($mode -eq 339 -and ($moves|Measure-Object -Maximum).Maximum -gt .06)){throw 'Vehicle jumped while parked or IV did not take over on initial entry'}
    }finally {& $run -Action Finish -Stop | Out-Null}
}