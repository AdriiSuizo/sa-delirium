param(
    [Parameter(Mandatory)][string]$Installed,
    [ValidateSet('Classic','Izzy','IV')][string[]]$Modes = @('Classic','Izzy','IV')
)
$ErrorActionPreference = 'Stop'
$tools = Join-Path $Installed 'tools'
. (Join-Path $tools 'Test-Harness.ps1')
$run = Join-Path $tools 'Run-Case.ps1'
$command = Join-Path $tools 'Send-NativeCommand.ps1'
$ini = Join-Path $Installed 'SA/DeliriumSA.ini'
$original = [IO.File]::ReadAllBytes($ini)

function Reaction([int]$Mode) {
    $reply = & $command -Action probe_vehicle_reaction -Mode $Mode
    if ($reply.result -notmatch '^vehicle_reaction_result_(\d+)$') {
        throw "Fixture $Mode returned $($reply.result)"
    }
    [int]$Matches[1]
}

$priorMode = $env:GTASA_VEHICLE_LAB
$priorMenu = $env:GTASA_VEHICLE_MENU_TEST
try {
    $env:GTASA_VEHICLE_LAB = 'traffic'
    $env:GTASA_VEHICLE_MENU_TEST = '1'
    foreach ($mode in $Modes) {
        $settings = [Text.Encoding]::UTF8.GetString($original)
        $settings = $settings -replace '(?m)^Mode=(Classic|Izzy|IV)\r?$', "Mode=$mode"
        if ($settings -notmatch '(?m)^\[VehicleDeformation\]\r?$') {
            $settings += "`r`n[VehicleDeformation]`r`nEnabled=1`r`n"
        } else {
            $settings = $settings -replace '(?m)(^\[VehicleDeformation\]\r?\nEnabled=)0', '${1}1'
        }
        [IO.File]::WriteAllText($ini, $settings)
        & $run -Action Start -Name ("deformation-" + $mode.ToLowerInvariant()) | Out-Null
        try {
            $case = Get-Content (Join-Path $Installed 'logs/workflow-state.json') -Raw | ConvertFrom-Json
            $loaded = Wait-ProjectFreeRoam -Case $case -Timeout 45 -AllowAmbientPhone
            if ($loaded.current_area -ne 0) {
                & $command -Action probe_house_transition | Out-Null
                Wait-ProjectFreeRoam -Case $case -Area 0 -Timeout 25 -AllowAmbientPhone | Out-Null
            }
            $expected = @{Classic=0; Izzy=1; IV=2}[$mode]
            if ((Reaction 385) -ne $expected -or (Reaction 384) -ne 1) { throw "Unexpected startup mode for $mode" }
            if ((Reaction 386) -ne [int]($mode -eq 'Izzy')) { throw "Unexpected Izzy module state for $mode" }
            $null = Reaction 380
            $deadline = [DateTime]::UtcNow.AddSeconds(12)
            do {
                Start-Sleep -Milliseconds 400
                $panels = Reaction 381
            } until ($panels -gt 0 -or [DateTime]::UtcNow -gt $deadline)
            if ($panels -lt 1 -or (Reaction 382) -ne 1) { throw "Expected damaged mesh and an untouched twin in $mode ($panels panels)" }
            $null = Reaction 388
            Start-Sleep -Milliseconds 600
            $shot = & $command -Action probe_visual_frame
            if ($shot.result -eq 'visual_frame_ok') {
                Copy-Item -LiteralPath (Join-Path $Installed "logs/bridge/visual-frame-$($case.pid).bmp") -Destination (Join-Path $case.directory 'deformed.bmp')
            }
            if ((Reaction 383) -ne 1 -or (Reaction 381) -ne 0) { throw "Repair failed in $mode" }
            if ((Reaction 346) -ne 33) { throw 'Options did not open' }
            Start-Sleep -Milliseconds 450
            if ((Reaction 355) -ne 0 -or (Reaction 385) -ne $expected) { throw "Deformation toggle affected $mode" }
            if ((Reaction 355) -ne 1 -or (Reaction 385) -ne $expected) { throw "Deformation toggle failed in $mode" }
            if ((Reaction 348) -ne 0) { throw 'The game did not resume' }
            "PASS $mode, native collision, $panels deformed panels, untouched twin, repair and independent toggle"
        } finally {
            & $run -Action Finish -Stop | Out-Null
        }
    }
} finally {
    $env:GTASA_VEHICLE_LAB = $priorMode
    $env:GTASA_VEHICLE_MENU_TEST = $priorMenu
    [IO.File]::WriteAllBytes($ini, $original)
}
