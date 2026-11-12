param(
    [Parameter(Mandatory)][string]$Installed,
    [Parameter(Mandatory)][ValidateSet('before','after')][string]$Revision,
    [ValidateRange(300,317)][int]$FirstMode=300,
    [ValidateRange(300,317)][int]$LastMode=317,
    [switch]$Fleet,
    [ValidateRange(400,611)][int[]]$FleetModels,
    [switch]$CheckNitro
)
$ErrorActionPreference = 'Stop'
$tools = Join-Path $Installed 'tools'
$trace = Join-Path $Installed 'SA/DeliriumSA-local-v3/vehicle-lab.csv'
$events = Join-Path $Installed 'SA/DeliriumSA-local-v3/events.jsonl'
. (Join-Path $tools 'Test-Harness.ps1')
$run = Join-Path $tools 'Run-Case.ps1'
$command = Join-Path $tools 'Send-NativeCommand.ps1'
$previous = $env:GTASA_VEHICLE_LAB
$kind = if($Fleet){'fleet'}else{'grip'}
try {
    $env:GTASA_VEHICLE_LAB = 'iv'
    & $run -Action Start -Name "$kind-$Revision"
} finally { $env:GTASA_VEHICLE_LAB = $previous }
$case = Get-Content (Join-Path $Installed 'logs/workflow-state.json') -Raw | ConvertFrom-Json

function Measure-Slip($Rows) {
    $angles = @($Rows | ForEach-Object {
        [Math]::Atan2([Math]::Abs([double]$_.lateral_mps), [double]$_.forward_mps) * 180 / [Math]::PI
    } | Sort-Object)
    if ($angles.Count -lt 8) { throw 'Too few grounded samples during the turn' }
    [Math]::Round($angles[[Math]::Ceiling($angles.Count * .95) - 1], 2)
}

try {
    $snapshot = Join-Path $Installed 'logs/bridge/state.json'
    $deadline = (Get-Date).AddSeconds(25)
    do {
        Start-Sleep -Milliseconds 300
        $bridge = if (Test-Path $snapshot) { Get-Content $snapshot -Raw | ConvertFrom-Json }
    } while ((!$bridge -or $bridge.pid -ne $case.pid) -and (Get-Date) -lt $deadline)
    if (!$bridge -or $bridge.pid -ne $case.pid) { throw 'Bridge did not start' }
    $state = Wait-ProjectFreeRoam -Case $case -Timeout 35
    if ($state.current_area -eq 3) {
        & $command -Action probe_house_transition | Out-Null
        Wait-ProjectFreeRoam -Case $case -Area 0 -Timeout 25 | Out-Null
    }
    $scene = & $command -Action probe_grove_street
    if ($scene.result -ne 'grove_scene_result_0') { throw $scene.result }
    Start-Sleep -Seconds 3
    if($CheckNitro){
        $nitro=& $command -Action probe_vehicle_reaction -Mode 294
        if($nitro.result -ne 'vehicle_reaction_result_0'){throw "Nitro test returned $($nitro.result)"}
        'Nitro test passed: accelerator and native fire state left the bottle intact; dedicated press fired.'
    }

    $modes = if ($Fleet -and $FleetModels) {
        $FleetModels | ForEach-Object {400+$_}
    } elseif ($Fleet) {
        $models = Join-Path $Installed 'SA/data/vehicles.ide'
        foreach($line in Get-Content -LiteralPath $models){
            if($line -match '^\s*(\d+),\s*[^,]+,\s*[^,]+,\s*(car|mtruck|quad)\s*,'){
                $model=[int]$Matches[1]
                if($model -notin 432,486,564){400+$model}
            }
        }
    } else { $FirstMode..$LastMode }
    $minElapsed = if($Fleet){1100}else{5450}
    foreach ($mode in $modes) {
      try {
        $result = & $command -Action probe_vehicle_reaction -Mode $mode
        if ($result.result -notmatch '^vehicle_reaction_result_(\d+)$') { throw "Mode $mode failed: $($result.result)" }
        $ref = [int]$Matches[1]
        $deadline = (Get-Date).AddSeconds(25)
        do {
            Start-Sleep -Milliseconds 350
            if (!(Get-Process -Id $case.pid -ErrorAction SilentlyContinue)) { throw 'Game exited' }
            $last = Get-Content -LiteralPath $trace -Tail 1
            $elapsed = if ($last -match '^(\d+),') { [int]$Matches[1] } else { 0 }
        } while ($elapsed -lt $minElapsed -and (Get-Date) -lt $deadline)
        if ($elapsed -lt $minElapsed) { throw "Mode $mode trace ended at $elapsed ms" }
        Start-Sleep -Milliseconds 450
        $entry = Get-Content -LiteralPath $events -Tail 1200 |
            Where-Object { $_ -match '"event":"vehicle_lab_fixture"' } | Select-Object -Last 1 | ConvertFrom-Json
        if ($entry.pid -ne $case.pid -or $entry.ref -ne $ref) { throw "Mode $mode trace has no matching vehicle" }
        if ($Fleet -and $entry.model -ne ($mode - 400)) { throw "Mode $mode returned model $($entry.model)" }
        if ($entry.detail -notmatch '\bsource=([12])\b') { throw "Model $($entry.model) has no verified tuning source" }
        $source = if($Matches[1] -eq '1'){'iv'}else{'custom'}
        $rows = Import-Csv -LiteralPath $trace
        $coast = @($rows | Where-Object { [int]$_.ms -ge 2380 -and [int]$_.ms -lt 3050 -and
            [int]$_.grounded -eq 4 -and [double]$_.forward_mps -gt 5 })
        $power = @($rows | Where-Object { [int]$_.ms -ge 3070 -and [int]$_.ms -lt 3850 -and
            [int]$_.grounded -eq 4 -and [double]$_.forward_mps -gt 5 })
        $final = $rows[-1]
        Copy-Item -LiteralPath $trace -Destination (Join-Path $case.directory "$($entry.model).csv")
        if([int]$final.iv_steps -lt 10){throw "Only $($final.iv_steps) IV steps"}
        $summary = [ordered]@{
            mode=$mode; model=$entry.model; source=$source; mass_kg=[double]$final.mass;
            coast_slip_p95_deg=$(if(!$Fleet){Measure-Slip $coast});
            powered_slip_p95_deg=$(if(!$Fleet){Measure-Slip $power});
            coast_yaw_deg=$(if(!$Fleet){[Math]::Abs([double]$coast[-1].heading - [double]$coast[0].heading)});
            launch_mps=($rows | Where-Object {[int]$_.ms -lt 2250} | ForEach-Object {[double]$_.forward_mps} |
                Measure-Object -Maximum).Maximum;
            iv_steps=[int]$final.iv_steps; fallback_steps=[int]$final.fallback_steps
        } | ConvertTo-Json -Compress
        Add-Content -LiteralPath (Join-Path $case.directory 'grip.jsonl') -Value $summary
        $summary
      }catch{
        if(!(Get-Process -Id $case.pid -ErrorAction SilentlyContinue)){throw}
        $failure=[ordered]@{mode=$mode;error=$_.Exception.Message} | ConvertTo-Json -Compress
        Add-Content -LiteralPath (Join-Path $case.directory 'grip.jsonl') -Value $failure
        $failure
      }
    }
} finally { & $run -Action Finish -Stop | Out-Null }
