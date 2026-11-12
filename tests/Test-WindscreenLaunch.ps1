param([Parameter(Mandatory)][string]$Installed)
$ErrorActionPreference='Stop'
$tools=Join-Path $Installed 'tools'
. (Join-Path $tools 'Test-Harness.ps1')
$run=Join-Path $tools 'Run-Case.ps1'
$command=Join-Path $tools 'Send-NativeCommand.ps1'
function Reaction([int]$Mode){
    $reply=& $command -Action probe_vehicle_reaction -Mode $Mode
    if($reply.result -notmatch '^vehicle_reaction_result_(\d+)$'){throw "$Mode returned $($reply.result)"}
    [int]$Matches[1]
}
$settings=@{
    GTASA_VEHICLE_LAB='traffic'
    GTASA_WINDSCREEN_AUDIT='1'
    GTASA_WINDSCREEN_TEST_DRAW='1'
    GTASA_PROBE_CAR_PASSENGER='1'
}
$old=@{}
try{
    foreach($name in $settings.Keys){
        $old[$name]=[Environment]::GetEnvironmentVariable($name)
        [Environment]::SetEnvironmentVariable($name,$settings[$name])
    }
    & $run -Action Start -Name 'windscreen-frontal-two-occupants' | Out-Null
}finally{
    foreach($name in $old.Keys){[Environment]::SetEnvironmentVariable($name,$old[$name])}
}
$case=Get-Content (Join-Path $Installed 'logs/workflow-state.json') -Raw | ConvertFrom-Json
try{
    $state=Wait-ProjectFreeRoam -Case $case -Timeout 40 -AllowAmbientPhone
    if($state.current_area -eq 3){
        & $command -Action probe_house_transition | Out-Null
        Wait-ProjectFreeRoam -Case $case -Area 0 -Timeout 25 -AllowAmbientPhone | Out-Null
    }
    $grove=& $command -Action probe_grove_street
    if($grove.result -ne 'grove_scene_result_0'){throw $grove.result}
    Start-Sleep -Seconds 2
    $car=Reaction 344
    Start-Sleep -Milliseconds 900
    Reaction 345 | Out-Null
    Start-Sleep -Milliseconds 375
    $frame=& $command -Action probe_visual_frame
    if($frame.result -eq 'visual_frame_ok'){
        Copy-Item (Join-Path $Installed "logs/bridge/visual-frame-$($case.pid).bmp") (Join-Path $case.directory 'windscreen.bmp')
    }
    Start-Sleep -Seconds 4
    $log=Join-Path $Installed 'SA/DeliriumSA-local-v3/events.jsonl'
    $rows=@(Get-Content -LiteralPath $log -Tail 10000 |
        Where-Object {$_ -match '"event":"(probe_(windscreen_frontal|car_passenger)|windscreen_[^"]+|cj_flight_voice)"'} |
        ConvertFrom-Json | Where-Object pid -eq $case.pid)
    $rows | ConvertTo-Json | Set-Content (Join-Path $case.directory 'windscreen.json')
    foreach($seat in 0,2){
        $flight=$rows | Where-Object {$_.event -eq 'windscreen_ejection' -and $_.detail -match "vehicle_ref=$car\b.* seat=$seat\b"} |
            Select-Object -First 1
        if(!$flight -or $flight.detail -notmatch 'original_horizontal_mps=([\d.]+) horizontal_mps=([\d.]+)'){
            throw "Missing measured ejection for front seat $seat"
        }
        $before=[double]$Matches[1];$after=[double]$Matches[2]
        $release=$rows | Where-Object {$_.event -eq 'windscreen_release_velocity' -and $_.ref -eq $flight.ref} | Select-Object -First 1
        if(!$release -or $release.detail -notmatch '^horizontal_mps=([\d.]+)'){throw "No solver release velocity for seat $seat"}
        $actual=[double]$Matches[1]
        $clear=$rows | Where-Object {$_.event -eq 'windscreen_clear' -and $_.ref -eq $flight.ref -and $_.detail -eq 'timeout=0 parts_inside=0'}
        [ordered]@{seat=$seat;actor=$flight.ref;previous_mps=$before;release_mps=$actual;delta_mps=$actual-$before;clear=[bool]$clear} |
            ConvertTo-Json -Compress
        if($actual -lt $before+1 -or [Math]::Abs($actual-$after) -gt .1 -or !$clear){
            throw "Seat $seat did not clear the car with the stronger impulse"
        }
    }
}finally{& $run -Action Finish -Stop | Out-Null}