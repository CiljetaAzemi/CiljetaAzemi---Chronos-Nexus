param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateSet('small','medium','large','extreme')][string]$Size = 'small',
    [ValidateSet('standard','scarcity','production-cut','dynamic')][string]$Scenario = 'standard',
    [uint32]$Seed = 20261228,
    [ValidateRange(0.0166666666666667,1193)][double]$Hours = 8,
    [ValidateSet('adaptive','fixed','baseline','neighborhood','neighborhood-greedy','exact-hybrid','expanding-hybrid')][string]$Strategy = 'adaptive',
    [switch]$Welfare,
    [ValidateRange(5,4294967)][int]$Seconds,
    [switch]$Resume
)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $Executable).Path
if (!$Resume -and (Test-Path -LiteralPath $OutputDirectory)) { throw 'Choose a fresh output directory.' }
if ($Resume) { $root = (Resolve-Path -LiteralPath $OutputDirectory).Path }
else { $root = (New-Item -ItemType Directory -Path $OutputDirectory).FullName }
$milliseconds = [uint64][Math]::Round($Hours * 3600000)
if ($Seconds) { $milliseconds = [uint64]$Seconds * 1000 }
$common = @('--benchmark',$Size,"$Seed",'phase3',"--scenario=$Scenario",
    '--pruned-initial','--memory-budget-mib=1024','--reserve-ms=3000',
    '--feasibility-aware-allocator','--reverse-aid-bounds','--trace-plan',
    "--evaluation-time-reference=28800")
$search = switch ($Strategy) {
    'fixed' { '--long-search-fixed' }
    'neighborhood' { '--neighborhood-search' }
    'neighborhood-greedy' { '--neighborhood-greedy' }
    'exact-hybrid' { '--exact-hybrid' }
    'expanding-hybrid' { '--expanding-hybrid' }
    default { '--long-search' }
}
$checkpoint = Join-Path $root 'incumbent.json'
$progress = Join-Path $root 'progress.csv'
$manifest = Join-Path $root 'configuration.json'
$configuration = [ordered]@{size=$Size; scenario=$Scenario; seed=$Seed; strategy=$Strategy;
    welfare=$Welfare.IsPresent; executable_sha256=(Get-FileHash -LiteralPath $exe).Hash}
$identity = $configuration | ConvertTo-Json -Compress
if ($Resume) {
    if (!(Test-Path -LiteralPath $manifest) -or (Get-Content -LiteralPath $manifest -Raw).Trim() -cne $identity) {
        throw 'Run configuration or executable differs from the original run.'
    }
} else {
    Set-Content -LiteralPath $manifest -Value $identity
}
if ($Welfare) { $common += '--welfare-priority' }
$arguments = $common + "--time-budget-ms=$milliseconds"
if ($Strategy -ne 'baseline') {
    $arguments += @($search,"--checkpoint=$checkpoint","--search-progress=$progress",'--report-seconds=15')
    if ($Resume) { $arguments += '--resume-search' }
} elseif ($Resume) { throw 'The baseline does not write recoverable checkpoints.' }
if ($Resume -and !(Test-Path -LiteralPath $checkpoint)) { throw 'Checkpoint is missing.' }
$log = Join-Path $root ('run-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '.log')
& $exe @arguments | Tee-Object -FilePath $log
if ($LASTEXITCODE) { throw "Search returned $LASTEXITCODE." }
function Read-LastRecord($Path, $Prefix) {
    Get-Content -LiteralPath $Path | Where-Object { $_.StartsWith($Prefix + ',') } | Select-Object -Last 1
}
$status = Read-LastRecord $log 'ANYTIME'
$outcome = Read-LastRecord $log 'ANYTIME_OUTCOME'
if (!$status) { throw 'Executable returned without a planning result.' }
$simulationLog = $null
if (!$outcome -and $Strategy -ne 'baseline' -and (Test-Path -LiteralPath $checkpoint)) {
    $simulationLog = Join-Path $root "replay-$([IO.Path]::GetFileName($log))"
    & $exe @common $search "--checkpoint=$checkpoint" --replay-checkpoint --time-budget-ms=120000 |
        Tee-Object -FilePath $simulationLog
    if ($LASTEXITCODE) { throw 'Final checkpoint replay failed. The saved incumbent remains available.' }
    $outcome = Read-LastRecord $simulationLog 'ANYTIME_OUTCOME'
}
$s = $status.Split(',')
$result = [ordered]@{configuration=$configuration; requested_additional_seconds=$milliseconds / 1000;
    validated_plan=$s[1] -eq '1'; planning_complete=$s[2] -eq '1'; simulation_complete=[bool]$outcome;
    candidates_evaluated=[long]$s[6]; improvements=[long]$s[7];
    search_process_seconds=[double]$s[11]; peak_working_set_bytes=[long]$s[12]; stop_reason=$s[13];
    stop_stage=$s[14]; search_log=[IO.Path]::GetFileName($log); replay_log=$null; realized=$null}
if ($simulationLog) { $result.replay_log = [IO.Path]::GetFileName($simulationLog) }
if ($outcome) {
    $values = $outcome.Split(',')
    $fields = 'legacy','expanded','completed','failed','timely_service','critical_ending','total_ending',
        'unserved','transport','fuel','waste','risk','events_applied','essential_coverage','critical_unserved',
        'delivered','construction_cost','projects','essential_unserved'
    $result.realized = [ordered]@{}
    for ($i=0; $i -lt $fields.Count; ++$i) { $result.realized[$fields[$i]] = [double]$values[$i+1] }
    $outcomeLog = if ($simulationLog) { $simulationLog } else { $log }
    $details = Read-LastRecord $outcomeLog 'RESULT'
    if ($details) {
        $detail = $details.Split(',')
        $result.realized.distance = [double]$detail[10]
        $result.realized.graph_nodes_retained_plan_and_simulation = [long]$detail[14]
        $result.realized.unsupported_events = [long]$detail[18]
        $result.realized.replans = [long]$detail[19]
        $result.realized.average_replan_seconds = if ($detail[27] -ne 'NA') { [double]$detail[27] } else { $null }
    }
    $overall = Read-LastRecord $outcomeLog 'LONG_REALIZED_OVERALL'
    $result.realized.experimental_overall = if ($overall) { [double]$overall.Split(',')[1] } else { $null }
}
$result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'final-results.json')
if ($Strategy -ne 'baseline' -and (Test-Path -LiteralPath $progress)) {
    $rows = Import-Csv -LiteralPath $progress
    $curve = foreach ($second in @(0,60,300,900,1800,3600,7200,14400,28800)) {
        if ($second -gt [double]$rows[-1].seconds + 3) { continue }
        $row = if (!$second) { $rows[0] } else {
            $rows | Where-Object {[double]$_.seconds -le $second} | Select-Object -Last 1
        }
        if ($row) { $row | Select-Object @{n='target_seconds';e={$second}},* }
    }
    $curve | Export-Csv -NoTypeInformation -LiteralPath (Join-Path $root 'milestones.csv')
    $rows | Select-Object @{n='run';e={"$Size-$Seed-$Scenario"}},* |
        Export-Csv -NoTypeInformation -LiteralPath (Join-Path $root 'curve-data.csv')
    & (Join-Path $PSScriptRoot 'Plot-LongSearch.ps1') -ProgressCsv (Join-Path $root 'curve-data.csv') -OutputSvg (Join-Path $root 'convergence.svg')
}
if (!$outcome) { throw 'No completed event simulation. See final-results.json and the run logs.' }
