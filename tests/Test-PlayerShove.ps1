param([Parameter(Mandatory)][string]$Installed)
$ErrorActionPreference='Stop'
$tools=Join-Path $Installed 'tools'
. (Join-Path $tools 'Test-Harness.ps1')
$run=Join-Path $tools 'Run-Case.ps1'
$command=Join-Path $tools 'Send-NativeCommand.ps1'
function Invoke-ShoveFixture([int]$Mode) {
    $reply=& $command -Action probe_vehicle_reaction -Mode $Mode
    if($reply.result -notmatch '^vehicle_reaction_result_(\d+)$'){throw $reply.result}
    [int]$Matches[1]
}

& $run -Action Start -Name 'player-shove-contact' | Out-Null
$case=Get-Content (Join-Path $Installed 'logs/workflow-state.json') -Raw | ConvertFrom-Json
try {
    $state=Wait-ProjectFreeRoam -Case $case -Timeout 40 -AllowAmbientPhone
    if($state.current_area -eq 3){
        & $command -Action probe_house_transition | Out-Null
        Wait-ProjectFreeRoam -Case $case -Area 0 -Timeout 25 -AllowAmbientPhone | Out-Null
    }
    $scene=& $command -Action probe_grove_street
    if($scene.result -ne 'grove_scene_result_0'){throw $scene.result}
    $target=Invoke-ShoveFixture 172
    Start-Sleep -Milliseconds 450
    $cj=Invoke-ShoveFixture 174
    Start-Sleep -Seconds 3
    $events=Join-Path $Installed 'SA/DeliriumSA-local-v3/events.jsonl'
    $rows=@(Get-Content -LiteralPath $events -Tail 8000 | Where-Object {
        $_ -match '"event":"(player_shove_[^"]+|standing_owner_contact|animated_owner_contact_receipt|probe_standing_bump_[^"]+)"'
    } | ConvertFrom-Json | Where-Object {$_.pid -eq $case.pid -and $_.ref -in $target,$cj})
    $rows | ConvertTo-Json | Set-Content (Join-Path $case.directory 'shove-evidence.json')
    $contact=$rows | Where-Object {
        ($_.event -eq 'standing_owner_contact' -and $_.ref -eq $target -and $_.detail -match "other=$cj\b") -or
        ($_.event -eq 'animated_owner_contact_receipt' -and $_.ref -eq $target -and $_.detail -match "peer=$cj\b")
    } | Select-Object -First 1
    $shove=$rows | Where-Object {$_.event -eq 'player_shove_started' -and $_.ref -eq $cj -and $_.detail -match "other=$target\b"} |
        Select-Object -First 1
    if(!$contact -or !$shove -or $shove.time_ms -lt $contact.time_ms -or
       $shove.detail -notmatch 'solved_contact=1' -or ($rows | Where-Object event -eq 'player_shove_leadin')){
        throw 'The shove did not start on a solved contact with the target'
    }
    $contact,$shove | ConvertTo-Json -Compress
} finally {
    & $run -Action Finish -Stop | Out-Null
}
