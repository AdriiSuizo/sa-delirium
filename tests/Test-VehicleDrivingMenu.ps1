param([Parameter(Mandatory)][string]$Installed)
$ErrorActionPreference='Stop'
$tools=Join-Path $Installed 'tools'
. (Join-Path $tools 'Test-Harness.ps1')
$run=Join-Path $tools 'Run-Case.ps1'
$command=Join-Path $tools 'Send-NativeCommand.ps1'
$ini=Join-Path $Installed 'SA/DeliriumSA.ini'
$original=[IO.File]::ReadAllBytes($ini)
$events=Join-Path $Installed 'SA/DeliriumSA-local-v3/events.jsonl'

function Reaction([int]$Mode) {
    $reply=& $command -Action probe_vehicle_reaction -Mode $Mode
    if($reply.result -notmatch '^vehicle_reaction_result_(\d+)$'){throw "Mode $Mode returned $($reply.result)"}
    [int]$Matches[1]
}

function Start-Case([string]$Name) {
    $beforeTraffic=$env:GTASA_VEHICLE_LAB
    $beforeMenu=$env:GTASA_VEHICLE_MENU_TEST
    $env:GTASA_VEHICLE_LAB='traffic'
    $env:GTASA_VEHICLE_MENU_TEST='1'
    try{& $run -Action Start -Name $Name | Out-Null}
    finally{
        $env:GTASA_VEHICLE_LAB=$beforeTraffic
        $env:GTASA_VEHICLE_MENU_TEST=$beforeMenu
    }
    Get-Content (Join-Path $Installed 'logs/workflow-state.json') -Raw | ConvertFrom-Json
}

function Wait-InGrove($Case) {
    $state=Wait-ProjectFreeRoam -Case $Case -Timeout 40 -AllowAmbientPhone
    if($state.current_area -eq 3){
        & $command -Action probe_house_transition | Out-Null
        Wait-ProjectFreeRoam -Case $Case -Area 0 -Timeout 25 -AllowAmbientPhone | Out-Null
    }
    $scene=& $command -Action probe_grove_street
    if($scene.result -ne 'grove_scene_result_0'){throw $scene.result}
    Start-Sleep -Seconds 2
}

function Select-Driving([int]$Expected,$Case,[string]$Image) {
    if((Reaction 346) -ne 33){throw 'Options page did not open'}
    Start-Sleep -Milliseconds 350
    if((Reaction $(if($Expected){356}else{347})) -ne $Expected){throw 'Options row did not change the driving mode'}
    if($Image){
        Start-Sleep -Milliseconds 250
        $frame=& $command -Action probe_visual_frame
        if($frame.result -ne 'visual_frame_ok'){throw $frame.result}
        Copy-Item (Join-Path $Installed "logs/bridge/visual-frame-$($Case.pid).bmp") (Join-Path $Case.directory $Image)
    }
    if((Reaction 348) -ne 0){throw 'The game did not resume'}
    Start-Sleep -Milliseconds 650
}

try{
    $case=Start-Case 'vehicle-driving-menu-switch'
    try{
        Wait-InGrove $case
        $initial=Reaction 349
        if($initial -eq 0){Select-Driving 1 $case ''}
        $car=Reaction 350
        Start-Sleep -Milliseconds 650
        Select-Driving 0 $case 'driving-off.bmp'
        $ambient = @(1..6 | ForEach-Object { Reaction 357; Start-Sleep -Milliseconds 500 })
        if(($ambient | Measure-Object -Maximum).Maximum -lt 1){throw 'Ambient drivers disappeared after switching to Classic'}
        if((Reaction 346) -ne 33){throw 'Movement options did not open'}
        $movementBefore=Reaction 358
        $offOrOn=Reaction 351
        if((Reaction 358) -ne $offOrOn -or $offOrOn -eq $movementBefore){throw 'CJ movement did not change live'}
        if((Reaction 351) -ne $movementBefore -or (Reaction 358) -ne $movementBefore){throw 'CJ movement did not restore live'}
        if((Reaction 348) -ne 0){throw 'The game did not resume after changing CJ movement'}
        "PASS CJ movement switched twice in the same game"
        Select-Driving 1 $case 'driving-on.bmp'
        $rows=@(Get-Content -LiteralPath $events -Tail 8000 |
            Where-Object {$_ -match 'vehicle_menu_test_physics|vehicle_driving_mode|vehicle_menu_unavailable'} |
            ConvertFrom-Json | Where-Object pid -eq $case.pid)
        $rows | ConvertTo-Json | Set-Content (Join-Path $case.directory 'driving-menu.json')
        $physics=@($rows | Where-Object {$_.event -eq 'vehicle_menu_test_physics' -and $_.ref -eq $car})
        if(($physics.detail -join ',') -ne 'iv,classic,iv' -or ($rows | Where-Object event -eq 'vehicle_menu_unavailable')){
            throw 'The same vehicle did not switch live from IV to stock and back'
        }
        Select-Driving 0 $case ''
        "PASS live driving toggle on car $car"
    }finally{& $run -Action Finish -Stop | Out-Null}

    $restart=Start-Case 'vehicle-driving-menu-persisted'
    try{
        Wait-InGrove $restart
        if((Reaction 349) -ne 0){throw 'Off did not persist after restarting'}
        $car=Reaction 350
        Start-Sleep -Milliseconds 650
        Select-Driving 1 $restart ''
        $rows=@(Get-Content -LiteralPath $events -Tail 8000 |
            Where-Object {$_ -match 'vehicle_menu_test_physics|vehicle_driving_mode'} |
            ConvertFrom-Json | Where-Object pid -eq $restart.pid)
        $rows | ConvertTo-Json | Set-Content (Join-Path $restart.directory 'driving-menu.json')
        $physics=@($rows | Where-Object {$_.event -eq 'vehicle_menu_test_physics' -and $_.ref -eq $car})
        if(($physics.detail -join ',') -ne 'classic,iv'){throw 'The persisted state did not reach native suspension and then IV'}
        "PASS persisted Off and enabled again after restarting"
    }finally{& $run -Action Finish -Stop | Out-Null}
}finally{
    if(!(Get-Process gta_sa,gta-sa -ErrorAction SilentlyContinue)){
        [IO.File]::WriteAllBytes($ini,$original)
    }
}