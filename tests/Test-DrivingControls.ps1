param(
    [Parameter(Mandatory)][string]$Installed,
    [Parameter(Mandatory)][ValidateSet('motorcycle','bmx','aim','aim-bike','aim-inputs','police-follow','police-chase','civilian-follow')][string]$Scenario
)
$ErrorActionPreference = 'Stop'
$tools = Join-Path $Installed 'tools'
. (Join-Path $tools 'Test-Harness.ps1')
$run = Join-Path $tools 'Run-Case.ps1'
$command = Join-Path $tools 'Send-NativeCommand.ps1'
$bike = $Scenario -in 'motorcycle','bmx'
$env:GTASA_VEHICLE_LAB = 'traffic'
if ($bike) {
    $env:GTASA_WINDSCREEN_AUDIT = '1'
    $env:GTASA_WINDSCREEN_TEST_DV = '0.15'
    $env:GTASA_TEST_BIKE_MODEL = if ($Scenario -eq 'bmx') { '481' } else { '463' }
}
function Reaction([int]$Mode) {
    $ack = & $command -Action probe_vehicle_reaction -Mode $Mode
    if ($ack.result -notmatch '^vehicle_reaction_result_(\d+)$') { throw "Mode $Mode returned $($ack.result)" }
    [int]$Matches[1]
}

& $run -Action Start -Name "driving-$Scenario" | Out-Null
$case = Get-Content (Join-Path $Installed 'logs/workflow-state.json') -Raw | ConvertFrom-Json
try {
    $state = Wait-ProjectFreeRoam -Case $case -Timeout 40 -AllowAmbientPhone
    if ($state.current_area -eq 3) {
        & $command -Action probe_house_transition | Out-Null
        Wait-ProjectFreeRoam -Case $case -Area 0 -Timeout 25 -AllowAmbientPhone | Out-Null
    }
    $grove = & $command -Action probe_grove_street
    if ($grove.result -ne 'grove_scene_result_0') { throw $grove.result }
    Start-Sleep -Seconds 3
    if ($bike) {
        $rider = Reaction 298
        Start-Sleep -Seconds 1
        Reaction 252 | Out-Null
        Start-Sleep -Seconds 2
    } elseif ($Scenario -in 'aim','aim-bike') {
        $initial = Reaction $(if ($Scenario -eq 'aim-bike') {331} else {318})
        Reaction 319 | Out-Null
        Start-Sleep -Milliseconds 650
        Reaction 329 | Out-Null
        Start-Sleep -Milliseconds 350
        $aim = Reaction 323
        $frame = & $command -Action probe_visual_frame
        if ($frame.result -ne 'visual_frame_ok') { throw $frame.result }
        Copy-Item (Join-Path $Installed "logs/bridge/visual-frame-$($case.pid).bmp") (Join-Path $case.directory 'aim.bmp')
        $aimWithGas = Reaction 323
        Reaction 328 | Out-Null
        Reaction 320 | Out-Null
        Reaction 330 | Out-Null
        Start-Sleep -Milliseconds 2700
        $released = Reaction 321
        Start-Sleep -Milliseconds 850
        $throttle = Reaction 323
        Reaction 327 | Out-Null
        Start-Sleep -Milliseconds 800
        $hipFire = Reaction 322
        Start-Sleep -Milliseconds 350
        Reaction 323 | Out-Null
        if ($aim -ne $initial -or $aimWithGas -ne $aim -or $released -ge $aim -or $throttle -ne $released -or $hipFire -ge $throttle) {
            throw "Unexpected ammo through aim, fire and throttle: $initial $aim $aimWithGas $released $throttle $hipFire"
        }
    } elseif ($Scenario -eq 'aim-inputs') {
        $initial = Reaction 318
        Reaction 332 | Out-Null
        Start-Sleep -Milliseconds 1100
        $aim = Reaction 332
        Reaction 333 | Out-Null
        Start-Sleep -Milliseconds 850
        $fired = Reaction 334
        Start-Sleep -Milliseconds 450
        Reaction 323 | Out-Null
        Reaction 332 | Out-Null
        Start-Sleep -Milliseconds 450
        Reaction 335 | Out-Null
        Start-Sleep -Milliseconds 450
        Reaction 323 | Out-Null
        if ($initial -ne $aim -or $fired -ge $aim) {
            throw "Unexpected ammo for L1+R2 and L1+R1+R2: $initial $aim $fired"
        }
    } else {
        $follower = Reaction (@{'police-follow'=324;'police-chase'=325;'civilian-follow'=326}[$Scenario])
        Start-Sleep -Seconds 5
    }
    $events = Join-Path $Installed 'SA/DeliriumSA-local-v3/events.jsonl'
    $rows = @(Get-Content -LiteralPath $events -Tail 10000 | Where-Object {
        $_ -match '"event":"(windscreen_[^"]+|bike_preimpact_contact_matched|probe_driveby_(controls|native_pad|ray)|vehicle_iv_traffic_command|probe_traffic_pursuit_target)"'
    } | ConvertFrom-Json | Where-Object pid -eq $case.pid)
    $rows | ConvertTo-Json | Set-Content (Join-Path $case.directory 'driving-evidence.json')
    if ($bike) {
        $contact = $rows | Where-Object {$_.event -eq 'windscreen_two_wheeler_ignored' -and
            $_.detail -match "model=$env:GTASA_TEST_BIKE_MODEL\b"} | Select-Object -First 1
        $riderContact = $rows | Where-Object {$_.event -eq 'bike_preimpact_contact_matched' -and $_.ref -eq $rider}
        if (!$contact -or !$riderContact) { throw 'The car and the rider did not register the native contact' }
        if ($rows | Where-Object {$_.event -eq 'windscreen_ejection' -and
                $_.time_ms -ge $contact.time_ms -and $_.time_ms -le $contact.time_ms+180}) {
            throw 'The bike impact ejected the driver'
        }
        $contact | ConvertTo-Json -Compress
    } elseif ($Scenario -in 'aim','aim-bike') {
        $aimCommands = $rows | Where-Object {$_.event -eq 'probe_driveby_controls' -and
            $_.detail -match 'mode=329 .* fire=0 .* left=0 right=0 vehicle_x=0 vehicle_y=0 camera=1'}
        $fireCommands = $rows | Where-Object {$_.event -eq 'probe_driveby_controls' -and
            $_.detail -match 'mode=320 .* fire=1 .* left=0 right=0 vehicle_x=0 vehicle_y=0 camera=1'}
        $blocks = $rows | Where-Object {$_.event -eq 'probe_driveby_controls' -and
            $_.detail -match 'hydraulic_blocks=[1-9]\d*'}
        if (!$aimCommands -or !$fireCommands -or ($Scenario -eq 'aim' -and !$blocks)) {
            throw 'Missing scoped aim, fire or hydraulic evidence'
        }
        $rearShot = $rows | Where-Object {
            $_.event -eq 'probe_driveby_ray' -and
            $_.detail -match 'aim_dot=([\d.]+) heading_dot=(-[\d.]+) pitch=(-[\d.]+) speed_mps=([\d.]+) render_dot=([\d.]+) render_up_z=([\d.]+)' -and
            [double]$Matches[1] -ge .999 -and [double]$Matches[2] -lt -.25 -and
            [double]$Matches[3] -lt -.15 -and [double]$Matches[4] -gt 1 -and
            [double]$Matches[5] -ge .999 -and [double]$Matches[6] -gt 0
        }
        if (!$rearShot) { throw 'No camera-aligned shot backwards and down while driving' }
        $unstableView = $rows | Where-Object {
            $_.event -eq 'probe_driveby_ray' -and
            ($_.detail -notmatch 'render_dot=([\d.]+) render_up_z=([\d.]+)' -or
                [double]$Matches[1] -lt .999 -or [double]$Matches[2] -le 0)
        }
        if ($unstableView) { throw 'A shot used a different view from the rendered camera' }
        $normal = $rows | Where-Object {$_.event -eq 'probe_driveby_controls' -and
            $_.detail -match 'mode=323 .* camera=0 blend=0\.000000'} | Select-Object -Last 1
        if (!$normal) { throw 'The ordinary camera did not return' }
        $rows | Where-Object event -eq 'probe_driveby_controls' | ConvertTo-Json -Compress
        $rearShot | Select-Object -First 1 | ConvertTo-Json -Compress
    } elseif ($Scenario -eq 'aim-inputs') {
        $pad = $rows | Where-Object event -eq 'probe_driveby_native_pad'
        foreach ($expected in 'mode=332 weapon_slot=4 throttle=255 fire=0 vehicle_x=0 vehicle_y=0',
            'mode=333 weapon_slot=4 throttle=255 fire=1 vehicle_x=0 vehicle_y=0',
            'mode=334 weapon_slot=4 throttle=0 fire=0 vehicle_x=73 vehicle_y=-63',
            'mode=335 weapon_slot=0 throttle=0 fire=0 vehicle_x=73 vehicle_y=-63') {
            if ($expected -notin $pad.detail) { throw "Missing native pad response: $expected" }
        }
        $upward = @($rows | Where-Object {
            $_.event -eq 'probe_driveby_ray' -and
            $_.detail -match 'pitch=([\d.]+) .* render_dot=([\d.]+) render_up_z=([\d.]+)' -and
            [double]$Matches[1] -ge .6 -and [double]$Matches[2] -ge .999 -and [double]$Matches[3] -gt 0
        })
        if (!$upward) { throw 'Missing upward shot aligned with the rendered view' }
        $controls = $rows | Where-Object event -eq 'probe_driveby_controls'
        $switch = $controls | Where-Object {$_.detail -match 'mode=335 .* camera=1'}
        $last = $controls | Select-Object -Last 1
        if (!$switch -or $last.detail -notmatch 'mode=323 .* camera=0 blend=0\.000000') {
            throw 'The free camera did not exit when the drive-by weapon was put away'
        }
        $pad | ConvertTo-Json -Compress
        $upward | Select-Object -First 1 | ConvertTo-Json -Compress
    } else {
        $policy = if ($Scenario -eq 'police-chase') { 'intercept|direct' } else { 'follow' }
        $sample = $rows | Where-Object {$_.event -eq 'vehicle_iv_traffic_command' -and
            $_.ref -eq $follower -and $_.detail -match "policy=($policy)\b"} | Select-Object -First 1
        if (!$sample) { throw "The expected pursuit policy ($policy) was not observed" }
        $sample | ConvertTo-Json -Compress
    }
} finally {
    & $run -Action Finish -Stop | Out-Null
    Remove-Item Env:GTASA_VEHICLE_LAB,Env:GTASA_WINDSCREEN_AUDIT,Env:GTASA_WINDSCREEN_TEST_DV,Env:GTASA_TEST_BIKE_MODEL -ErrorAction SilentlyContinue
}
