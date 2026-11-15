function Get-StartupEvidence {
    param([string]$Sessions, [int]$ProcessId, [string[]]$ExistingSessions = @())
    # Windows recycles PIDs. Only a directory created by this launch is evidence.
    $matching = @(Get-ChildItem -LiteralPath $Sessions -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notin $ExistingSessions -and $_.Name -like "*-p$ProcessId" })
    if ($matching.Count -ne 1) {
        return [pscustomobject]@{state='pending';session=$null;events=@();failures=@()}
    }
    $directory = $matching[0].FullName
    $rows = @(Get-Content -LiteralPath (Join-Path $directory 'events.jsonl') -ErrorAction SilentlyContinue |
        ForEach-Object { try { $r = $_ | ConvertFrom-Json; if ($r.pid -eq $ProcessId) { $r } } catch {} })
    $failures = @($rows | Where-Object {
        $_.event -eq 'engine_error' -or $_.event -match '(^unsupported_executable$|^invalid_real_character_build$|_failed$|_hook_mismatch$)'
    })
    $state = if ($failures.Count) { 'startup_error' }
        elseif ($rows | Where-Object event -eq 'initialized') { 'initialized' }
        else { 'pending' }
    [pscustomobject]@{state=$state;session=$directory;events=$rows;failures=$failures}
}
