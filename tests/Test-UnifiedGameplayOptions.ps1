param([Parameter(Mandatory)][string]$Installed)
$ErrorActionPreference='Stop'
$tools=Join-Path $Installed 'tools'
. (Join-Path $tools 'Test-Harness.ps1')
$run=Join-Path $tools 'Run-Case.ps1'
$command=Join-Path $tools 'Send-NativeCommand.ps1'
$ini=Join-Path $Installed 'SA/DeliriumSA.ini'
$original=[IO.File]::ReadAllBytes($ini)

function Reaction([int]$Mode) {
    $reply=& $command -Action probe_vehicle_reaction -Mode $Mode
    if($reply.result -notmatch '^vehicle_reaction_result_(\d+)$'){throw "Mode $Mode returned $($reply.result)"}
    [int]$Matches[1]
}

$mode=$env:GTASA_VEHICLE_LAB
$menu=$env:GTASA_VEHICLE_MENU_TEST
try {
    $env:GTASA_VEHICLE_LAB='traffic'
    $env:GTASA_VEHICLE_MENU_TEST='1'
    & $run -Action Start -Name 'unified-gameplay-options' | Out-Null
    try {
        $case=Get-Content (Join-Path $Installed 'logs/workflow-state.json') -Raw | ConvertFrom-Json
        Wait-ProjectFreeRoam -Case $case -Timeout 40 -AllowAmbientPhone | Out-Null
        if((Reaction 346) -ne 33){throw 'The options page did not open'}
        Start-Sleep -Milliseconds 450
        if((Reaction 353) -ne 2 -or (Reaction 354) -ne 2){throw 'Expected the initial IV mode'}
        if((Reaction 347) -ne 0 -or (Reaction 353) -ne 0 -or (Reaction 354) -ne 0){throw 'Classic mode did not take effect'}
        if((Reaction 347) -ne 0 -or (Reaction 353) -ne 1 -or (Reaction 354) -ne 0){throw 'Izzy should be selected for restart'}
        if((Reaction 347) -ne 1 -or (Reaction 353) -ne 2 -or (Reaction 354) -ne 2){throw 'IV mode did not take effect'}
        if((Reaction 351) -ne 0 -or (Reaction 352) -ne 0){throw 'Gameplay options did not turn off'}
        $options=Get-Content -Raw -LiteralPath $ini
        if($options -notmatch '(?m)^\[Movement\]\r?\n(?:(?!\[).*\r?\n)*?NikoOnFoot=0\r?$' -or
           $options -notmatch '(?m)^\[Combat\]\r?\n(?:(?!\[).*\r?\n)*?Enabled=0\r?$'){throw 'Both gameplay settings must be persisted'}
        if((Reaction 351) -ne 1 -or (Reaction 352) -ne 1){throw 'Gameplay options did not turn back on'}
        if((Reaction 348) -ne 0){throw 'The game did not resume'}
        "PASS Classic, pending Izzy, IV, movement and combat menu rows"
    } finally {
        & $run -Action Finish -Stop | Out-Null
    }
} finally {
    $env:GTASA_VEHICLE_LAB=$mode
    $env:GTASA_VEHICLE_MENU_TEST=$menu
    [IO.File]::WriteAllBytes($ini,$original)
}
