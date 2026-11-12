param(
    [Parameter(Mandatory)][string]$Installed,
    [Parameter(Mandatory)][string]$Label,
    [switch]$Airborne
)
$ErrorActionPreference = 'Stop'
$tools = Join-Path $Installed 'tools'
$physics = Join-Path $Installed 'SA/DeliriumSA-local-v3'
. (Join-Path $tools 'Test-Harness.ps1')
$run = Join-Path $tools 'Run-Case.ps1'
$command = Join-Path $tools 'Send-NativeCommand.ps1'
& $run -Action Start -Name $Label | Out-Null
$case = Get-Content (Join-Path $Installed 'logs/workflow-state.json') -Raw | ConvertFrom-Json
try {
    $state = Wait-ProjectFreeRoam -Case $case -Timeout 35 -AllowAmbientPhone
    if ($state.current_area -eq 3) {
        & $command -Action probe_house_transition | Out-Null
        Wait-ProjectFreeRoam -Case $case -Area 0 -Timeout 25 -AllowAmbientPhone | Out-Null
    }
    $scene = & $command -Action probe_grove_street
    if ($scene.result -ne 'grove_scene_result_0') { throw $scene.result }
    Start-Sleep -Seconds 3
    $mode = if ($Airborne) { 297 } else { 85 }
    $blast = & $command -Action probe_vehicle_reaction -Mode $mode
    if ($blast.result -notmatch '^vehicle_reaction_result_(\d+)$') { throw $blast.result }
    $ped = [int]$Matches[1]
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $motion = while ($clock.ElapsedMilliseconds -lt 5000) {
        Start-Sleep -Milliseconds 100
        try { $state = Get-Content (Join-Path $physics 'state.json') -Raw | ConvertFrom-Json }
        catch { continue }
        if ($state.pid -ne $case.pid) { continue }
        foreach ($agent in ($state.agents | Where-Object ref -eq $ped)) {
            [pscustomobject]@{
                sample_ms=$clock.ElapsedMilliseconds; x=$agent.com[0]; y=$agent.com[1];
                z=$agent.com[2]; speed_mps=$agent.speed; peak_rise_m=$agent.peak_com_rise;
                peak_up_mps=$agent.peak_up_speed
            }
        }
    }
    if (!$motion) { throw 'The player never entered physical simulation' }
    $motion | Export-Csv -NoTypeInformation (Join-Path $case.directory 'blast-motion.csv')
    $events = @(Get-Content (Join-Path $physics 'events.jsonl') -Tail 5000 | Where-Object {
        $_ -match '"event":"(body_ground_audio|cj_flight_voice|probe_midrange_explosion|player_vehicle_physics_started)"'
    } | ConvertFrom-Json | Where-Object {$_.pid -eq $case.pid -and $_.ref -eq $ped})
    $events | ConvertTo-Json | Set-Content (Join-Path $case.directory 'blast-events.json')
    $voice = @($events | Where-Object {$_.event -eq 'cj_flight_voice' -and $_.detail -match '\bspeech=\d+\b'}).Count
    $landings = @($events | Where-Object event -eq 'body_ground_audio').Count
    $origin = $motion[0]
    $range = ($motion | ForEach-Object {
        [Math]::Sqrt(($_.x - $origin.x) * ($_.x - $origin.x) + ($_.y - $origin.y) * ($_.y - $origin.y))
    } | Measure-Object -Maximum).Maximum
    [ordered]@{
        pid=$case.pid; initial_health_after_blast=$blast.health;
        peak_rise_m=($motion | Measure-Object peak_rise_m -Maximum).Maximum;
        peak_up_mps=($motion | Measure-Object peak_up_mps -Maximum).Maximum;
        observed_horizontal_m=[Math]::Round($range, 3);
        peak_sampled_speed_mps=($motion | Measure-Object speed_mps -Maximum).Maximum;
        accepted_voice_events=$voice; ground_audio_events=$landings
    } | ConvertTo-Json | Tee-Object -FilePath (Join-Path $case.directory 'result.json')
    if ($Airborne -and (!$landings -or !$voice)) { throw 'Missing physical landing sound or airborne CJ voice' }
} finally { & $run -Action Finish -Stop | Out-Null }
