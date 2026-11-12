param(
    [Parameter(Mandatory)][string]$Installed,
    [Parameter(Mandatory)][ValidateSet('classic','iv','traffic')][string]$Mode,
    [Parameter(Mandatory)][ValidateSet('drive','road','road-medium','ramp','steep','pursuit','roll','route','hood','hood-high','hood-low','cj-light','cj-heavy','cj-short','cj-moving-roof','cj-moving-trunk','wheel','crash','curve-admiral','curve-comet','swerve-admiral','corner90-admiral','corner90-comet')][string]$Scenario,
    [switch]$BaselineSuspension,
    [switch]$PreviousDriving,
    [switch]$NativeNpcDriving
)
$ErrorActionPreference = 'Stop'
if ($Scenario -eq 'pursuit' -and $Mode -ne 'traffic') { throw 'Pursuit requires traffic mode' }
$hoodDrop = $Scenario -in 'hood-high','hood-low'
$cjDrop = $Scenario -in 'cj-light','cj-heavy','cj-short'
$movingDrop = $Scenario -in 'cj-moving-roof','cj-moving-trunk'
if (($hoodDrop -or $cjDrop -or $movingDrop -or $Scenario -eq 'road-medium') -and $Mode -eq 'classic') {
    throw 'Vehicle load checks require Delirium GTA IV suspension'
}
$tools = Join-Path $Installed 'tools'
$physics = Join-Path $Installed 'SA/DeliriumSA-local-v3'
$trace = Join-Path $physics 'vehicle-lab.csv'
. (Join-Path $tools 'Test-Harness.ps1')
$oldMode = $env:GTASA_VEHICLE_LAB
$oldBaseline = $env:GTASA_VEHICLE_LAB_BASELINE
$oldDriving = $env:GTASA_VEHICLE_LAB_SLOW
$oldNpcDriving = $env:GTASA_VEHICLE_NATIVE_AI
try {
    $env:GTASA_VEHICLE_LAB = $Mode
    $env:GTASA_VEHICLE_LAB_BASELINE = if ($BaselineSuspension) { '1' } else { $null }
    $env:GTASA_VEHICLE_LAB_SLOW = if ($PreviousDriving) { '1' } else { $null }
    $env:GTASA_VEHICLE_NATIVE_AI = if ($NativeNpcDriving) { '1' } else { $null }
    $suffix = if ($BaselineSuspension) { '-baseline' } else { '' }
    if ($PreviousDriving) { $suffix += '-previous' }
    if ($NativeNpcDriving) { $suffix += '-native-ai' }
    & (Join-Path $tools 'Run-Case.ps1') -Action Start -Name "vehicle-$Mode-$Scenario$suffix"
} finally {
    $env:GTASA_VEHICLE_LAB = $oldMode
    $env:GTASA_VEHICLE_LAB_BASELINE = $oldBaseline
    $env:GTASA_VEHICLE_LAB_SLOW = $oldDriving
    $env:GTASA_VEHICLE_NATIVE_AI = $oldNpcDriving
}
$case = Get-Content (Join-Path $Installed 'logs/workflow-state.json') -Raw | ConvertFrom-Json
$command = Join-Path $tools 'Send-NativeCommand.ps1'
try {
    $loaded = Wait-ProjectFreeRoam -Case $case -Timeout 40
    if ($loaded.current_area -eq 3) {
        & $command -Action probe_house_transition | Out-Null
        Wait-ProjectFreeRoam -Case $case -Area 0 -Timeout 30 | Out-Null
    } elseif ($loaded.current_area -ne 0) { throw 'Checkpoint must load into free roam' }
    $scene = & $command -Action probe_grove_street
    if ($scene.result -ne 'grove_scene_result_0') { throw $scene.result }
    Start-Sleep -Seconds 4
    Wait-ProjectFreeRoam -Case $case -Area 0 -Timeout 30 | Out-Null
    $fixtureMode = @{drive=280;road=281;ramp=282;steep=283;pursuit=284;roll=285;route=286;wheel=287;hood=288;'hood-high'=356;'hood-low'=356;crash=290;
        'cj-light'=360;'cj-heavy'=361;'cj-short'=360;'cj-moving-roof'=364;'cj-moving-trunk'=364;'road-medium'=368;
        'curve-admiral'=336;'curve-comet'=337;'swerve-admiral'=341;'corner90-admiral'=342;'corner90-comet'=343}[$Scenario]
    $fixture = & $command -Action probe_vehicle_reaction -Mode $fixtureMode
    if ($fixture.result -notmatch '^vehicle_reaction_result_(\d+)$') { throw $fixture.result }
    $target = [int]$Matches[1]
    if ($Scenario -eq 'wheel') {
        Start-Sleep -Seconds 3
        $placed = & $command -Action probe_vehicle_reaction -Mode 289
        if ($placed.result -notmatch '^vehicle_reaction_result_\d+$') { throw $placed.result }
    }

    if ($hoodDrop) {
        Start-Sleep -Seconds 1
        $dropMode = if ($Scenario -eq 'hood-high') { 357 } else { 358 }
        $drop = & $command -Action probe_vehicle_reaction -Mode $dropMode
        if ($drop.result -ne "vehicle_reaction_result_$target") { throw $drop.result }
    }
    if ($cjDrop) {
        Start-Sleep -Milliseconds 1450
        $dropMode = if ($Scenario -eq 'cj-short') { 363 } else { 362 }
        $drop = & $command -Action probe_vehicle_reaction -Mode $dropMode
        if ($drop.result -ne "vehicle_reaction_result_$target") { throw $drop.result }
    }
    if ($movingDrop) {
        Start-Sleep -Milliseconds 1450
        $roll = & $command -Action probe_vehicle_reaction -Mode 367
        if ($roll.result -ne "vehicle_reaction_result_$target") { throw $roll.result }
        Start-Sleep -Milliseconds 200
        $dropMode = if ($Scenario -eq 'cj-moving-roof') { 365 } else { 366 }
        $drop = & $command -Action probe_vehicle_reaction -Mode $dropMode
        if ($drop.result -ne "vehicle_reaction_result_$target") { throw $drop.result }
    }
    $images = switch ($Scenario) {
        'drive' { 1000, 2400, 3100, 5500 }
        {$_ -like 'corner90-*'} { 2200, 3000, 3700, 4450 }
        'swerve-admiral' { 2100, 2550, 3020, 3510 }
        'roll' { 2350, 3050, 3650, 4250, 5350, 6850 }
        {$_ -in 'road','road-medium'} { 250, 400, 850, 1500, 4100 }
        'pursuit' { 4500, 10000, 18000, 25500 }
        'route' { 4000, 10000, 17000, 25000, 33000 }
        'hood' { 200, 650, 1400, 2600, 4600 }
        {$_ -in 'hood-high','hood-low'} { 150, 420, 640, 860, 1050, 1280, 1800 }
        {$_ -like 'cj-*'} { 220, 520, 780, 1000, 1290, 1700, 2400 }
        'wheel' { 300, 850, 1500, 3500, 6000 }
        'crash' { 300, 750, 1200, 2400, 4500 }
        {$_ -like 'curve-*'} { 2000, 2350, 2700, 3000 }
        default { 1800, 2900, 3900, 5300, 7600 }
    }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    foreach ($ms in $images) {
        $wait = $ms - [int]$timer.ElapsedMilliseconds
        if ($wait -gt 0) { Start-Sleep -Milliseconds $wait }
        $frame = & $command -Action probe_visual_frame
        if ($frame.result -eq 'visual_frame_ok') {
            $image = Join-Path $Installed "logs/bridge/visual-frame-$($case.pid).bmp"
            Copy-Item $image (Join-Path $case.directory "frame-$ms.bmp")
        }
    }
    if ($hoodDrop) {
        $unload = & $command -Action probe_vehicle_reaction -Mode 359
        if ($unload.result -ne "vehicle_reaction_result_$target") { throw $unload.result }
    }
    Start-Sleep -Seconds 5
    $events = @(Get-Content (Join-Path $physics 'events.jsonl') |
        Select-String -SimpleMatch ('"pid":' + $case.pid + ',') |
        ForEach-Object { $_.Line | ConvertFrom-Json })
    $init = [Array]::FindLastIndex([object[]]$events, [Predicate[object]]{ param($event) $event.event -eq 'initialized' })
    if ($init -lt 0) { throw 'Missing runtime initialization' }
    $events = @($events | Select-Object -Skip $init)
    $events | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $case.directory 'events.json')
    $state = Get-Content (Join-Path $physics 'state.json') -Raw | ConvertFrom-Json
    if ($state.pid -ne $case.pid -or $state.errors -ne 0) { throw 'Invalid physics state' }
    if ($Scenario -eq 'pursuit') {
        $setup = $events | Where-Object event -eq 'vehicle_lab_pursuit_setup' | Select-Object -First 1
        if (!$setup) { throw 'Missing native wanted-level setup' }
        $steps = @($events | Where-Object { $_.event -eq 'vehicle_iv_ai_step' -and $_.time_ms -ge $setup.time_ms })
        $police = @($steps | Where-Object {
            $_.detail -match 'role=police mission=([2-6]|21|26) ' -and
            $_.detail -match 'gas=([0-9.]+)' -and [double]$Matches[1] -gt 0.02 -and
            $_.detail -match 'mps=([0-9.]+)' -and [double]$Matches[1] -gt 2
        })
        $civilian = @($steps | Where-Object {
            $_.detail -match 'role=civilian ' -and $_.detail -match 'mps=([0-9.]+)' -and [double]$Matches[1] -gt 2
        })
        $player = @($steps | Where-Object { $_.ref -eq $target -and $_.detail -match 'role=player ' })
        $promoted = @($events | Where-Object event -eq 'vehicle_iv_native_promotion')
        $pursuing = @($police | Group-Object ref | Where-Object Count -ge 3)
        if (!$player.Count -or !$civilian.Count -or !$pursuing.Count -or !$promoted.Count) {
            throw 'Missing player, moving civilian, police pursuit or traffic promotion evidence'
        }
        [ordered]@{status='passed';mode=$Mode;scenario=$Scenario;pid=$case.pid;
            player_steps=$player.Count;civilian_steps=$civilian.Count;pursuit_steps=$police.Count;
            police_cars=@($pursuing.Name);promoted_cars=@($promoted.ref | Sort-Object -Unique).Count;
            result_path=$case.directory} |
            ConvertTo-Json | Tee-Object -FilePath (Join-Path $case.directory 'result.json')
        return
    }
    if (!(Test-Path -LiteralPath $trace) -or (Get-Item $trace).LastWriteTime -lt ([DateTimeOffset]::Parse($case.started)).LocalDateTime) {
        throw 'Missing current car telemetry'
    }
    Copy-Item -LiteralPath $trace -Destination (Join-Path $case.directory 'car.csv')
    $rows = @(Import-Csv (Join-Path $case.directory 'car.csv'))
    if ($rows.Count -lt 30) { throw 'Too few vehicle samples' }
    $final = $rows[-1]
    if ($Mode -ne 'classic' -and ([int]$final.iv_steps -lt 20 -or [int]$final.native_pairs_replaced -lt 40)) {
        throw 'IV did not supply forces and replace the native wheel pairs'
    }
    if ($Mode -ne 'classic' -and [int]$final.iv_turn_calls -lt 20) {
        throw 'The IV rotation was not applied to the real chassis'
    }
    if ($Mode -ne 'classic' -and $Scenario -in 'drive','roll','ramp','steep' -and [int]$final.iv_rk4_pose_calls -lt 10) {
        throw 'The unobstructed car never used its integrated RK4 orientation'
    }
    if ($Mode -eq 'classic' -and [int]$final.iv_steps -ne 0) { throw 'Classic case used IV' }
    if ($Scenario -like 'curve-*') {
        $launch = $rows | Where-Object {[int]$_.ms -ge 2100 -and [int]$_.ms -lt 2250} | Select-Object -Last 1
        $turn = @($rows | Where-Object {[int]$_.ms -ge 2360 -and [int]$_.ms -le 3000 -and [int]$_.grounded -ge 2})
        if (!$launch -or $turn.Count -lt 15 -or [double]$turn[0].forward_mps -lt 13) {
            throw 'The vehicle did not enter the turn on the expected road and speed'
        }
        $heading = ([double]$turn[-1].heading - [double]$turn[0].heading + 540) % 360 - 180
        [ordered]@{launch_kmh=[double]$launch.forward_mps * 3.6;entry_kmh=[double]$turn[0].forward_mps * 3.6;
            heading_delta_deg=[Math]::Abs($heading);mean_steer_deg=($turn|Measure-Object steer -Average).Average * 180 / [Math]::PI;
            exit_kmh=[double]$turn[-1].forward_mps * 3.6;peak_lateral_mps=($turn|ForEach-Object {[Math]::Abs([double]$_.lateral_mps)}|Measure-Object -Maximum).Maximum} |
            ConvertTo-Json | Tee-Object -FilePath (Join-Path $case.directory 'curve.json')
    }
    if ($Scenario -like 'corner90-*') {
        $turn=@($rows | Where-Object {[int]$_.ms -ge 2350 -and [int]$_.ms -le 4600})
        $entry=$turn[0]
        $measure=@(foreach($row in $turn){
            $dx=[double]$row.x-[double]$entry.x
            $dy=[double]$row.y-[double]$entry.y
            [pscustomobject]@{
                ms=[int]$row.ms; turn=([double]$row.heading-[double]$entry.heading+540)%360-180
                reach_m=[Math]::Sqrt($dx*$dx+$dy*$dy)
                speed_mps=[double]$row.forward_mps; grounded=[int]$row.grounded
            }
        })
        $peak=$measure | Sort-Object turn -Descending | Select-Object -First 1
        $end=$measure | Where-Object {$_.turn -ge 90} | Select-Object -First 1
        [ordered]@{entry_kmh=[double]$entry.forward_mps*3.6;max_heading_deg=$peak.turn;
            time_to_90_ms=if($end){$end.ms-[int]$entry.ms};reach_at_90_m=$end.reach_m;
            speed_at_90_kmh=if($end){$end.speed_mps*3.6};min_grounded_wheels=($measure|Measure-Object grounded -Minimum).Minimum} |
            ConvertTo-Json | Tee-Object -FilePath (Join-Path $case.directory 'corner90.json')
        if(!$end -or $end.ms-[int]$entry.ms -gt 2100 -or $end.speed_mps -lt 4 -or $end.reach_m -gt 14.8){
            throw 'The car did not complete a tight 90 degree turn at the test entry speed'
        }
    }
    if ($Scenario -eq 'swerve-admiral') {
        $swerve=@($rows | Where-Object {[int]$_.ms -ge 2350 -and [int]$_.ms -le 4200})
        $launch=$rows | Where-Object {[int]$_.ms -ge 2100 -and [int]$_.ms -lt 2250} | Select-Object -Last 1
        $roll=($swerve | ForEach-Object {[Math]::Abs([double]$_.roll)} | Measure-Object -Maximum).Maximum
        $minWheels=($swerve|Measure-Object grounded -Minimum).Minimum
        [ordered]@{launch_kmh=[double]$launch.forward_mps*3.6;max_roll_deg=$roll;min_grounded_wheels=$minWheels} |
            ConvertTo-Json | Tee-Object -FilePath (Join-Path $case.directory 'swerve.json')
        $left=$swerve | Where-Object {[double]$_.steer -lt -.1} | Select-Object -First 1
        $right=$swerve | Where-Object {[double]$_.steer -gt .1} | Select-Object -First 1
        if(!$left -or !$right -or $roll -gt 15 -or $minWheels -lt 2 -or [double]$launch.forward_mps -lt 6.2) {
            throw 'The Admiral did not pass the alternating steering and launch checks'
        }
    }
    if ($Scenario -eq 'wheel') {
        if ([int]$final.body_wheel_steps -lt 1 -or !($events | Where-Object event -eq 'wheel_body_impulse')) {
            throw 'Missing finite body support with the IV road wheels still active'
        }
    }
    if ($Scenario -eq 'hood') {
        $pitch = ($rows | ForEach-Object { [Math]::Abs([double]$_.pitch - [double]$rows[0].pitch) } |
            Measure-Object -Maximum).Maximum
        if ($pitch -lt .1 -or !($events | Where-Object event -eq 'external_vehicle_committed')) {
            throw 'Missing body weight transferred to the live chassis and suspension'
        }
    }
    if ($hoodDrop) {
        $start = $events | Where-Object { $_.event -eq 'vehicle_lab_fixture' -and $_.ref -eq $target } | Select-Object -First 1
        $released = $events | Where-Object { $_.event -eq 'vehicle_hood_drop' -and $_.ref -eq $target } | Select-Object -First 1
        $removed = $events | Where-Object { $_.event -eq 'vehicle_hood_unloaded' -and $_.ref -eq $target } | Select-Object -First 1
        if (!$start -or !$released -or !$removed) { throw 'Missing hood-drop phase markers' }
        $releaseMs = $released.time_ms - $start.time_ms
        $removeMs = $removed.time_ms - $start.time_ms
        $before = $rows | Where-Object { [int]$_.ms -lt $releaseMs } | Select-Object -Last 1
        $loaded = @($rows | Where-Object { [int]$_.ms -gt $releaseMs -and [int]$_.ms -lt $removeMs })
        if (!$before -or !$loaded.Count) { throw 'Missing unloaded or loaded wheel samples' }
        $deepest = $loaded | Sort-Object { [double]$_.z } | Select-Object -First 1
        $front = $loaded | Sort-Object { [double]$_.compression_fl } -Descending | Select-Object -First 1
        $dip = [double]$before.z - [double]$deepest.z
        $left = [double]$front.compression_fl - [double]$before.compression_fl
        $right = [double]$front.compression_fr - [double]$before.compression_fr
        $loads = @($events | Where-Object { $_.event -eq 'road_car_motion' -and
            $_.time_ms -lt $removed.time_ms -and $_.detail -match "vehicle_ref=$target " -and
            $_.detail -match 'hull_jz=(-[0-9.]+)' -and [double]$Matches[1] -lt -1 })
        # The wheel hook stops emitting rows when native idle takes over.
        $rest = $events | Where-Object { $_.event -eq 'road_car_motion' -and
            $_.time_ms -ge ($removed.time_ms + 600) -and $_.detail -match "vehicle_ref=$target " } | Select-Object -Last 1
        if (!$rest -or $rest.detail -notmatch ' vz=(-?[0-9.]+) z=(-?[0-9.]+) nose_degrees=(-?[0-9.]+) ') {
            throw 'Missing resting chassis telemetry'
        }
        $atRest = [Math]::Abs([double]$Matches[1]) -lt .015
        $rise = [double]$Matches[2] - [double]$deepest.z
        $pitchAfter = [double]$Matches[3]
        [ordered]@{ chassis_dip_cm=100*$dip; rebound_cm=100*$rise; front_left_compression_cm=100*$left;
            front_right_compression_cm=100*$right; pitch_before_deg=[double]$before.pitch;
            pitch_under_load_deg=[double]$front.pitch; pitch_after_deg=$pitchAfter;
            roll_at_last_wheel_step_deg=[double]$final.roll; load_samples=$loads.Count } |
            ConvertTo-Json | Tee-Object -FilePath (Join-Path $case.directory 'hood-drop.json')
        if (!$atRest -or $loads.Count -lt 2 -or $dip -lt .025 -or $rise -lt .008 -or $left -lt .035 -or $left -lt ($right + .002) -or
            [int]$deepest.grounded -lt 3 -or [Math]::Abs($pitchAfter - [double]$before.pitch) -gt .2 -or
            [Math]::Abs([double]$final.roll - [double]$before.roll) -gt .2) {
            throw 'The loaded corner did not compress, rebound and settle on the live suspension'
        }
    }
    if ($cjDrop) {
        $start = $events | Where-Object { $_.event -eq 'vehicle_lab_fixture' -and $_.ref -eq $target } | Select-Object -First 1
        $released = $events | Where-Object { $_.event -eq 'vehicle_cj_hood_drop' -and $_.ref -eq $target } | Select-Object -First 1
        if (!$start -or !$released -or $released.detail -notmatch 'player_ref=(\d+)') { throw 'Missing CJ drop marker' }
        $cj = [int]$Matches[1]
        $motion = @(foreach ($entry in $events) {
            if ($entry.event -ne 'road_car_motion' -or $entry.time_ms -lt $released.time_ms -or
                $entry.detail -notmatch "vehicle_ref=$target .+ vz=(-?[0-9.]+) z=(-?[0-9.]+) nose_degrees=(-?[0-9.]+) .+ hull_jz=(-?[0-9.]+) ") { continue }
            [pscustomobject]@{ ms=$entry.time_ms; vz=[double]$Matches[1]; z=[double]$Matches[2];
                pitch=[double]$Matches[3]; impulse=[double]$Matches[4]; detail=$entry.detail }
        })
        $impact = $motion | Where-Object impulse -lt -5 | Select-Object -First 1
        $before = $rows | Where-Object { [int]$_.ms -lt ($released.time_ms - $start.time_ms) } | Select-Object -Last 1
        if (!$impact -or !$before) { throw 'Missing the actual hood impulse or the unloaded suspension' }
        $after = @($motion | Where-Object { $_.ms -ge $impact.ms })
        $bottom = $after | Where-Object { $_.ms -le ($impact.ms + 1100) } | Sort-Object z | Select-Object -First 1
        $wheels = $rows | Where-Object { [int]$_.ms -ge ($impact.ms - $start.time_ms - 60) -and
            [int]$_.ms -le ($impact.ms - $start.time_ms + 1100) }
        $front = $wheels | Sort-Object { [double]$_.compression_fl } -Descending | Select-Object -First 1
        if (!$front) { throw 'Missing active front spring during CJ impact' }
        $dip = $motion[0].z - $bottom.z
        $frontTravel = [double]$front.compression_fl - [double]$before.compression_fl
        $frontRebound = ($rows | Where-Object { [int]$_.ms -gt [int]$front.ms -and [int]$_.ms -le ([int]$front.ms + 1400) } |
            Measure-Object compression_fl -Minimum).Minimum
        $rebound = $after | Where-Object { $_.ms -gt $bottom.ms -and $_.ms -le ($bottom.ms + 1400) } |
            Sort-Object z -Descending | Select-Object -First 1
        $rise = if ($rebound) { $rebound.z - $bottom.z } else { 0.0 }
        $pitch = [Math]::Abs([double]$front.pitch - [double]$before.pitch)
        if ($Scenario -eq 'cj-short') {
            $shared = $events | Where-Object { $_.event -eq 'external_pair_authority_receipt' -and $_.ref -eq $cj -and
                $_.time_ms -ge $released.time_ms -and $_.detail -match "vehicle_ref=$target " -and
                $_.detail -match 'double_authority_evidence=0' } | Select-Object -First 1
            $high = $events | Where-Object { $_.event -eq 'player_high_fall_started' -and $_.ref -eq $cj }
            $support = $after | Where-Object { $_.ms -gt ($impact.ms + 500) -and $_.impulse -lt -1 } | Select-Object -First 1
            if (!$shared -or $high -or !$support) { throw 'Short drop must carry CJ with the shared animated contact' }
        } else {
            $high = $events | Where-Object { $_.event -eq 'player_high_fall_started' -and $_.ref -eq $cj -and
                $_.time_ms -ge $released.time_ms -and $_.time_ms -le $impact.ms } | Select-Object -First 1
            if (!$high -or !($impact.detail -match "body_ref=$cj ") -or $rise -lt .001) {
                throw 'Missing CJ ragdoll impact or spring rebound'
            }
        }
        [ordered]@{ model=$released.model; mass_kg=[double]$before.mass; chassis_dip_cm=100*$dip;
            front_left_compression_cm=100*$frontTravel; nose_dip_deg=$pitch; chassis_rebound_cm=100*$rise;
            front_left_rebound_cm=if($null -ne $frontRebound){100*([double]$front.compression_fl-$frontRebound)};
            rebound_peak_ms=if($rebound){$rebound.ms-$bottom.ms}; final_pitch_deg=$motion[-1].pitch;
            peak_downward_impulse_ns=-($after|Measure-Object impulse -Minimum).Minimum } |
            ConvertTo-Json | Tee-Object -FilePath (Join-Path $case.directory 'cj-hood.json')
        if ($dip -lt .002 -or $frontTravel -lt .005 -or [int]$front.grounded -lt 3) {
            throw 'CJ contact did not move the chassis and front spring on the road'
        }
    }
    if ($movingDrop) {
        $start = $events | Where-Object { $_.event -eq 'vehicle_lab_fixture' -and $_.ref -eq $target } | Select-Object -First 1
        $released = $events | Where-Object { $_.event -eq 'vehicle_cj_moving_drop' -and $_.ref -eq $target } | Select-Object -First 1
        if (!$start -or !$released -or $released.detail -notmatch 'player_ref=(\d+)') { throw 'Missing moving drop marker' }
        $cj = [int]$Matches[1]
        $hit = $events | Where-Object { $_.event -eq 'probe_vehicle_body_contact' -and $_.ref -eq $cj -and
            $_.time_ms -ge $released.time_ms -and $_.detail -match "^vehicle_ref=$target " -and
            $_.detail -match ' impulse=[-0-9.]+,[-0-9.]+,(-[0-9.]+)$' -and [double]$Matches[1] -lt -8 } |
            Select-Object -First 1
        if (!$hit -or $hit.detail -notmatch ' local=([-0-9.]+),([-0-9.]+),([-0-9.]+) ') { throw 'Missing downward CJ load on the car' }
        $x = [double]$Matches[1]; $y = [double]$Matches[2]; $z = [double]$Matches[3]
        $roof = $Scenario -eq 'cj-moving-roof'
        $correctSurface = if ($roof) { $x -lt -.1 -and [Math]::Abs($y) -lt 1 -and $z -gt .60 }
                          else { $x -gt .1 -and $y -lt -1 -and $z -gt .18 }
        if (!$correctSurface) { throw 'CJ missed the chosen roof or trunk contact zone' }
        $high = $events | Where-Object { $_.event -eq 'player_high_fall_started' -and $_.ref -eq $cj -and
            $_.time_ms -ge $released.time_ms -and $_.time_ms -le $hit.time_ms } | Select-Object -First 1
        $hitMs = $hit.time_ms - $start.time_ms
        $before = $rows | Where-Object { [int]$_.ms -ge ($hitMs - 100) -and [int]$_.ms -lt $hitMs } | Select-Object -Last 1
        $after = @($rows | Where-Object { [int]$_.ms -ge $hitMs -and [int]$_.ms -le ($hitMs + 500) -and [int]$_.grounded -ge 3 })
        if (!$high -or !$before -or $after.Count -lt 6 -or [double]$before.forward_mps -lt 3) {
            throw 'Missing physical CJ contact while the road wheels were still moving'
        }
        $compression = [ordered]@{}
        foreach ($wheel in 'fl','fr','rl','rr') {
            $field = "compression_$wheel"
            $compression[$wheel] = 100*(($after | Measure-Object $field -Maximum).Maximum - [double]$before.$field)
        }
        $loadedWheel = if ($roof) { 'rl' } else { 'rr' }
        $field = "compression_$loadedWheel"
        $peak = $after | Sort-Object { [double]$_.$field } -Descending | Select-Object -First 1
        $recovery = ($rows | Where-Object { [int]$_.ms -gt [int]$peak.ms -and [int]$_.ms -lt ([int]$peak.ms + 1000) -and
            [int]$_.grounded -ge 3 } | Measure-Object $field -Minimum).Minimum
        [ordered]@{ speed_at_impact_kmh=3.6*[double]$before.forward_mps; contact_local_m=@($x,$y,$z);
            extra_compression_cm=$compression; measured_rebound_wheel=$loadedWheel;
            recovered_travel_cm=if ($null -ne $recovery) {100*([double]$peak.$field-$recovery)} } |
            ConvertTo-Json -Depth 4 | Tee-Object -FilePath (Join-Path $case.directory 'moving-drop.json')
        $localResponse = if ($roof) { $compression.fl -gt ($compression.fr + .5) -and $compression.rl -gt ($compression.rr + .5) }
                         else { $compression.rr -gt ([Math]::Max($compression.fl,$compression.fr) + .5) }
        if (!$localResponse -or $compression[$loadedWheel] -lt .5 -or $null -eq $recovery -or
            [double]$peak.$field - $recovery -lt .002) {
            throw 'The loaded side or rear axle did not compress and rebound on the road'
        }
    }
    if ($Scenario -eq 'crash') {
        $peerSpeed = ($rows | Measure-Object peer_speed_mps -Maximum).Maximum
        $peerAngle = ($rows | ForEach-Object { [Math]::Abs([double]$_.peer_roll) + [Math]::Abs([double]$_.peer_pitch) } |
            Measure-Object -Maximum).Maximum
        if ($peerSpeed -lt .15 -or $peerAngle -lt .1) { throw 'Missing reciprocal car collision and chassis response' }
    }
    if ($Scenario -eq 'route') {
        $distances = @($rows | ForEach-Object { [Math]::Sqrt(([double]$_.x-2345)*([double]$_.x-2345)+([double]$_.y+1575)*([double]$_.y+1575)) })
        $travelled = 0.0
        $stalled = 0.0
        for($i=1;$i -lt $rows.Count;$i++){
            $dx=[double]$rows[$i].x-[double]$rows[$i-1].x
            $dy=[double]$rows[$i].y-[double]$rows[$i-1].y
            $travelled += [Math]::Sqrt($dx*$dx+$dy*$dy)
            if([double]$rows[$i].gas -gt .3 -and [Math]::Abs([double]$rows[$i].forward_mps) -lt .2){
                $stalled += ([double]$rows[$i].ms-[double]$rows[$i-1].ms)/1000
            }
        }
        $arrivalIndex = [Array]::FindIndex([double[]]$distances,[Predicate[double]]{param($d) $d -lt 8})
        [ordered]@{initial_distance_m=$distances[0];closest_distance_m=($distances|Measure-Object -Minimum).Minimum;
            final_distance_m=$distances[-1];travelled_m=$travelled;stalled_with_throttle_seconds=$stalled;
            arrival_seconds=if($arrivalIndex -ge 0){[double]$rows[$arrivalIndex].ms / 1000}} |
            ConvertTo-Json | Set-Content (Join-Path $case.directory 'route.json')
        if($travelled -lt 20 -or ($distances|Measure-Object -Minimum).Minimum -ge 8){
            throw 'The native driver did not reach the road destination'
        }
    }
    if ($Scenario -eq 'roll') {
        $rest = @($rows | Where-Object { [int]$_.ms -ge 1600 -and [int]$_.ms -le 2150 })
        if (!$rest.Count) { throw 'Missing stationary baseline before the steering reversal' }
        $baseline = ($rest | Measure-Object roll -Average).Average
        $first = @($rows | Where-Object { [int]$_.ms -ge 2700 -and [int]$_.ms -lt 3150 -and [int]$_.grounded -ge 3 })
        $second = @($rows | Where-Object { [int]$_.ms -ge 3400 -and [int]$_.ms -lt 3850 -and [int]$_.grounded -ge 3 })
        $firstPeak = ($first | ForEach-Object { [double]$_.roll - $baseline } |
            Sort-Object { [Math]::Abs($_) } -Descending | Select-Object -First 1)
        $secondPeak = ($second | ForEach-Object { [double]$_.roll - $baseline } |
            Sort-Object { [Math]::Abs($_) } -Descending | Select-Object -First 1)
        if (!$first.Count -or !$second.Count -or [Math]::Abs($firstPeak) -lt .2 -or
            [Math]::Abs($secondPeak) -lt .2 -or $firstPeak * $secondPeak -ge 0 -or
            [Math]::Abs([double]$final.roll - $baseline) -gt 1.5) {
            throw "Expected opposite roll under steering reversal followed by settling. Peaks: $firstPeak / $secondPeak deg"
        }
        $entry = $rows | Where-Object { [double]$_.steer -gt 0 } | Select-Object -First 1
        if (!$entry -or [Math]::Abs([double]$entry.forward_mps - 6) -gt 0.15) {
            throw 'Expected the same 6 m/s steering entry speed'
        }
        $neutral = @($rows | Where-Object { [int]$_.ms -ge 3850 -and [int]$_.ms -lt 6000 })
        if (!$neutral.Count -or ($neutral | Where-Object { [int]$_.grounded -lt 3 })) {
            throw 'The release measurement needs an uninterrupted run on the road'
        }
        $rebound = ($neutral | ForEach-Object { -[Math]::Sign($secondPeak) * ([double]$_.roll - $baseline) } |
            Measure-Object -Maximum).Maximum
        $afterRelease = @($rows | Where-Object { [int]$_.ms -ge [int]$neutral[0].ms })
        $settledAt = $null
        foreach ($row in $afterRelease) {
            if ([Math]::Abs([double]$row.roll - $baseline) -gt .15) { $settledAt = $null }
            elseif ($null -eq $settledAt) { $settledAt = [int]$row.ms }
        }
        if ($null -ne $settledAt -and [int]$final.ms - $settledAt -lt 400) { $settledAt = $null }
        $tunedSteps = [int]$final.admiral_suspension_steps
        $replacedDamping = [int]$final.native_damping_replaced
        if ($Mode -ne 'classic' -and !$BaselineSuspension -and ($tunedSteps -lt 20 -or $replacedDamping -lt 20)) {
            throw 'The comparison did not exercise the imported suspension and replace native angular damping'
        }
        if ($BaselineSuspension -and $tunedSteps) { throw 'The baseline used the imported suspension' }
        [ordered]@{baseline_deg=$baseline;first_peak_deg=$firstPeak;second_peak_deg=$secondPeak;
            steering_entry_mps=[double]$entry.forward_mps;
            resting_height_m=($rest | Measure-Object z -Average).Average;
            roll_inertia_kg_m2=[double]$entry.inertia_y;
            rebound_deg=[Math]::Max(0.0,[double]$rebound);
            settle_seconds=if ($null -ne $settledAt) { ($settledAt - [int]$neutral[0].ms) / 1000 };
            settled_before_braking=($null -ne $settledAt -and $settledAt -lt 6000);
            final_deg=[double]$final.roll;final_ms=[int]$final.ms;
            admiral_suspension_steps=$tunedSteps;native_damping_replaced=$replacedDamping} |
            ConvertTo-Json | Set-Content (Join-Path $case.directory 'roll.json')
    }
    if ($Scenario -eq 'drive') {
        $peak = ($rows | ForEach-Object { [double]$_.forward_mps } | Measure-Object -Maximum).Maximum
        if ($peak -lt 6 -or [Math]::Abs([double]$final.forward_mps) -gt 0.3) {
            throw 'Expected actual acceleration followed by a complete stop'
        }
        if ($Mode -ne 'classic') {
            $rest = $rows | Where-Object { [int]$_.ms -ge 500 } | Select-Object -First 1
            $low = ($rows | Where-Object { [double]$_.gas -gt 0 } |
                ForEach-Object { [double]$_.z } | Measure-Object -Minimum).Minimum
            if ([double]$rest.z - $low -gt 0.12) { throw 'Chassis sank on the flat-road acceleration test' }
            $frontAtRest = ([double]$rest.compression_fl + [double]$rest.compression_fr) / 2
            $frontAfterBrake = ([double]$final.compression_fl + [double]$final.compression_fr) / 2
            if ($frontAfterBrake - $frontAtRest -gt 0.035 -or
                [double]$rest.pitch - [double]$final.pitch -gt 1.1) {
                throw 'Front suspension stayed compressed after stopping with the brake held'
            }
            $load = $rows | Where-Object { [double]$_.load_fl -gt 1000 } | Select-Object -First 1
            $powered = $rows | Where-Object { [double]$_.gas -gt 0 -and [double]$_.drive -gt 0 } | Select-Object -First 1
            $steering = $rows | Where-Object {
                [double]$_.steer -ne 0 -and [Math]::Abs([double]$_.lateral_force) -gt 100
            } | Select-Object -First 1
            if (!$load -or !$powered -or !$steering) { throw 'Missing IV spring, transmission or steering force' }
        }
    }
    if ($Scenario -in 'road','road-medium') {
        $hit = @($events | Where-Object ref -eq $target)
        $hit | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $case.directory 'pedestrian.json')
        foreach ($required in 'accepted_vehicle_contact','damage_activation','road_solved_reaction') {
            if (!($hit | Where-Object event -eq $required)) { throw "Missing pedestrian event $required" }
        }
        $start = $events | Where-Object event -eq 'vehicle_lab_fixture' | Select-Object -First 1
        $impact = $hit | Where-Object event -eq 'accepted_vehicle_contact' | Select-Object -First 1
        if (!$start -or $impact.time_ms -le $start.time_ms) { throw 'Missing car start or impact clock' }
        if ($Mode -ne 'classic') {
            $approach = $rows | Where-Object {
                [int]$_.ms -lt ($impact.time_ms - $start.time_ms) -and [int]$_.iv_steps -gt 10
            } | Select-Object -First 1
            if (!$approach) { throw 'Impact was not preceded by IV wheel steps' }
        }
        if ($Scenario -eq 'road-medium') {
            if ($impact.detail -notmatch 'closing_mps=([0-9.]+)') { throw 'Missing closing speed' }
            $closing = [double]$Matches[1]
            $contact = $hit | Where-Object { $_.event -eq 'probe_vehicle_body_contact' -and
                $_.time_ms -ge $impact.time_ms -and $_.time_ms -le ($impact.time_ms + 150) -and
                $_.detail -match "^vehicle_ref=$($start.ref) local=[-0-9.]+,([0-9.]+),[-0-9.]+ impulse=[-0-9.]+,[-0-9.]+,(-[0-9.]+)$" -and
                [double]$Matches[1] -gt 1 -and [double]$Matches[2] -lt -8 } | Select-Object -First 1
            $hitMs = $impact.time_ms - $start.time_ms
            $before = $rows | Where-Object { [int]$_.ms -lt $hitMs } | Select-Object -Last 1
            $after = @($rows | Where-Object { [int]$_.ms -ge $hitMs -and [int]$_.ms -le ($hitMs + 450) -and [int]$_.grounded -ge 3 })
            if (!$contact -or !$before -or $after.Count -lt 6 -or $closing -lt 10 -or $closing -gt 18) {
                throw 'Missing medium-speed contact at the front of the rolling car'
            }
            $front = ($after | ForEach-Object { ([double]$_.compression_fl + [double]$_.compression_fr)/2 } |
                Measure-Object -Maximum).Maximum - ([double]$before.compression_fl + [double]$before.compression_fr)/2
            $rear = ($after | ForEach-Object { ([double]$_.compression_rl + [double]$_.compression_rr)/2 } |
                Measure-Object -Maximum).Maximum - ([double]$before.compression_rl + [double]$before.compression_rr)/2
            [ordered]@{ closing_kmh=3.6*$closing; front_extra_compression_cm=100*$front; rear_extra_compression_cm=100*$rear } |
                ConvertTo-Json | Tee-Object -FilePath (Join-Path $case.directory 'road-medium.json')
            if ($front -lt .008 -or $front -le $rear + .004) { throw 'The pedestrian hit did not load the front axle' }
        }
    }
    if ($Scenario -in 'ramp','steep') {
        $placed = $events | Where-Object { $_.event -eq 'vehicle_lab_ramp' -and $_.ref -eq $target } | Select-Object -First 1
        if (!$placed -or $placed.detail -notmatch 'crest_x=(\d+\.\d+)') { throw 'Ramp placement not verified' }
        $crest = [double]$Matches[1]
        $baseZ = [double]$rows[0].z
        $climb = $rows | Where-Object { [int]$_.ramp_wheels -ge 2 -and [double]$_.z -gt ($baseZ + 1.1) } | Select-Object -First 1
        $top = $rows | Where-Object { [double]$_.x -ge $crest -and [double]$_.z -gt ($baseZ + 1.6) } | Select-Object -First 1
        $landing = $rows | Where-Object {
            [double]$_.x -gt ($crest + 3.5) -and [int]$_.grounded -ge 2 -and [double]$_.z -lt ($baseZ + 0.8)
        } | Select-Object -First 1
        if (!$climb -or !$top -or !$landing) { throw 'Missing physical climb, crest crossing or landing' }
        if ($Mode -ne 'classic' -and [int]$final.iv_ramp_steps -lt 10) { throw 'Static ramp never used IV wheel forces' }
    }
    [ordered]@{status='passed';mode=$Mode;scenario=$Scenario;pid=$case.pid;target=$target;
        samples=$rows.Count;iv_steps=[int]$final.iv_steps;native_pairs=[int]$final.native_pairs_replaced;
        fallback_steps=[int]$final.fallback_steps;iv_ramp_steps=[int]$final.iv_ramp_steps;
        iv_turn_calls=[int]$final.iv_turn_calls;iv_rk4_pose_calls=[int]$final.iv_rk4_pose_calls;
        body_wheel_steps=[int]$final.body_wheel_steps;
        result_path=$case.directory} |
        ConvertTo-Json | Tee-Object -FilePath (Join-Path $case.directory 'result.json')
} finally {
    & (Join-Path $tools 'Run-Case.ps1') -Action Finish -Stop | Out-Null
}
