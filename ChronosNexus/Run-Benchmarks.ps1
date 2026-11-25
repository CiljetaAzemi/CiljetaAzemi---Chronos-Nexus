[CmdletBinding()]
param(
    [string]$Executable = "$PSScriptRoot\x64\Release\ChronosNexus.exe",
    [ValidateSet('small', 'medium', 'large', 'extreme')]
    [string[]]$Sizes = @('small', 'medium', 'large', 'extreme'),
    [uint32]$Seed = 20261118,
    [ValidateSet('custom', 'earliest', 'basic', 'all-routes', 'greedy-build', 'stress', 'stress-all-routes', 'inventory', 'inventory-uncached', 'phase2', 'phase3')]
    [string[]]$Modes = @('custom'),
    [ValidateRange(1, 86400)]
    [int]$TimeoutSeconds = 600,
    [ValidateRange(64, 1048576)]
    [int]$MemoryMiB = 1024,
    [string]$OutputDirectory = "$PSScriptRoot\benchmark-results\runs",
    [switch]$TracePlan,
    [ValidateSet('balanced', 'security', 'service')]
    [string]$ObjectiveProfile = 'balanced',
    [ValidateSet('standard', 'scarcity', 'production-cut')]
    [string]$Scenario = 'standard',
    [switch]$CivilizationMetrics,
    [switch]$FeasibilityAwareAllocator,
    [switch]$LegacyAidSearch,
    [switch]$ExperimentalAllocator,
    [switch]$AllocationMetrics
)

$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $Executable).Path
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$csv = Join-Path $OutputDirectory 'results.csv'
$fields = @(
    'planned_aid', 'scheduled', 'rejected', 'completed', 'failed', 'delivered',
    'ending_shortage', 'stockout_amount', 'waste', 'distance', 'fuel', 'transportation_cost',
    'risk', 'graph_nodes_explored', 'allocation_route_queries', 'ship_route_checks',
    'events_applied', 'events_unsupported', 'route_replans', 'reroutes', 'replan_seconds',
    'construction_cost', 'construction_value', 'construction_seconds', 'civilization_score',
    'engine_seconds', 'average_replan_seconds'
)
$qualityFields = @(
    'recovered_scheduled', 'recovered_completed', 'revised_suppliers', 'inventory_failures',
    'weighted_timely_cargo', 'weighted_demand', 'weighted_timely_aid', 'weighted_aid_demand',
    'reservation_checks'
)
$includeCivilization = $CivilizationMetrics -or $Modes -contains 'phase3'
$includeQuality = $includeCivilization -or $Modes -contains 'inventory' -or $Modes -contains 'inventory-uncached' -or $Modes -contains 'phase2'
$civilizationFields = @('objective_profile','scenario','objective_value','security','critical_security',
    'input_service','infrastructure_benefit','operating_penalty','waste_penalty','risk_penalty',
    'critical_shortage','critical_unserved','plans_evaluated','plans_feasible','forecast_objective')
$includeLocal = $Modes -contains 'phase2'
$localFields = @('local_trials', 'local_accepted', 'local_recovered', 'local_arcs', 'local_pruned_arcs')
$dimensions = @{
    small = 100, 500, 50
    medium = 1000, 10000, 500
    large = 10000, 100000, 5000
    extreme = 100000, 1000000, 50000
}
foreach ($size in $Sizes) {
    foreach ($mode in $Modes) {
        $tag = if ($includeCivilization) { "-$Scenario-$ObjectiveProfile" } else { '' }
        $id = "$size-$Seed-$mode$tag-$([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))"
        $stdout = Join-Path $OutputDirectory "$id.stdout.txt"
        $stderr = Join-Path $OutputDirectory "$id.stderr.txt"
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $peak = 0L
        $lastLine = ''
        $status = 'failed'
        $exitCode = 'NA'
        Write-Host "Starting $id (limit $TimeoutSeconds s, $MemoryMiB MiB working set)"
        $process = $null
        try {
            $arguments = @('--benchmark', $size, "$Seed", $mode)
            if ($TracePlan) { $arguments += '--trace-plan' }
            if ($includeCivilization) { $arguments += "--objective=$ObjectiveProfile" }
            if ($Scenario -ne 'standard') { $arguments += "--scenario=$Scenario" }
            if ($FeasibilityAwareAllocator) { $arguments += '--feasibility-aware-allocator' }
            if ($LegacyAidSearch) { $arguments += '--legacy-aid-search' }
            if ($ExperimentalAllocator) { $arguments += '--experimental-allocator' }
            $process = Start-Process -FilePath $exe -ArgumentList $arguments `
                -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru -WindowStyle Hidden
            $null = $process.Handle
            while ($true) {
                $process.Refresh()
                if ($process.HasExited) {
                    try { $peak = [math]::Max($peak, $process.PeakWorkingSet64) } catch {}
                    $exitCode = $process.ExitCode
                    if ($exitCode -eq 0) { $status = 'complete' }
                    break
                }
                try {
                    $peak = [math]::Max($peak, $process.PeakWorkingSet64)
                } catch {
                    # A short run can exit between Refresh and the metric read.
                }
                $latest = Get-Content -LiteralPath $stdout -Tail 8 -ErrorAction SilentlyContinue |
                    Where-Object { $_ -like 'PROGRESS,*' } | Select-Object -Last 1
                if ($latest -and $latest -ne $lastLine) {
                    $lastLine = $latest
                    Write-Host "$latest,peak_working_set_bytes=$peak"
                }
                if ($peak -gt ($MemoryMiB * 1MB) -or $watch.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
                    $status = if ($peak -gt ($MemoryMiB * 1MB)) { 'memory_limit' } else { 'timeout' }
                    if (!$process.HasExited) {
                        try { $process.Kill() } catch [InvalidOperationException] {}
                    }
                    $process.WaitForExit()
                    $exitCode = $process.ExitCode
                    break
                }
                Start-Sleep -Milliseconds 200
            }
        } catch {
            Write-Warning $_.Exception.Message
            if ($process -and !$process.HasExited) {
                $process.Kill()
                $process.WaitForExit()
            }
        } finally {
            if ($process) { $process.Dispose() }
        }

        $lines = @(Get-Content -LiteralPath $stdout -ErrorAction SilentlyContinue)
        $memory = $lines | Where-Object { $_ -match '^MEMORY,\d+$' } | Select-Object -Last 1
        if ($memory) {
            $peak = [math]::Max($peak, [long]($memory -split ',')[1])
            if ($status -eq 'complete' -and $peak -gt ($MemoryMiB * 1MB)) {
                $status = 'memory_limit'
            }
        }
        $last = ($lines | Where-Object { $_ -like 'PROGRESS,*' } | Select-Object -Last 1) -split ','
        $result = ($lines | Where-Object { $_ -like 'RESULT,*' } | Select-Object -Last 1) -split ','
        $quality = ($lines | Where-Object { $_ -like 'QUALITY,*' } | Select-Object -Last 1) -split ','
        $local = ($lines | Where-Object { $_ -like 'LOCAL,*' } | Select-Object -Last 1) -split ','
        $civilization = ($lines | Where-Object { $_ -like 'CIVILIZATION,*' } | Select-Object -Last 1) -split ','
        if ($status -eq 'complete' -and $includeCivilization -and $civilization.Count -ne ($civilizationFields.Count + 1)) {
            $status = 'missing_civilization'
        }
        if ($status -eq 'complete' -and $result.Count -ne ($fields.Count + 1)) {
            $status = 'missing_result'
        }
        if ($status -eq 'complete' -and $includeQuality -and $quality.Count -ne ($qualityFields.Count + 1)) {
            $status = 'missing_quality'
        }
        if ($status -eq 'complete' -and $mode -eq 'phase2' -and $local.Count -ne ($localFields.Count + 1)) {
            $status = 'missing_local'
        }
        $counts = $dimensions[$size]
        $row = [ordered]@{
            size = $size
            seed = $Seed
            mode = $mode
            planets = $counts[0]
            routes = $counts[1]
            input_missions = $counts[2]
            status = $status
            exit_code = $exitCode
            runtime_limit_seconds = $TimeoutSeconds
            working_set_limit_mib = $MemoryMiB
            wall_seconds = $watch.Elapsed.TotalSeconds.ToString('F3', [Globalization.CultureInfo]::InvariantCulture)
            peak_working_set_bytes = if ($peak -gt 0) { $peak } else { 'NA' }
            last_stage = if ($last.Count -eq 5) { $last[1] } else { 'NA' }
            last_progress_seconds = if ($last.Count -eq 5) { $last[2] } else { 'NA' }
            processed_allocation_rows = if ($last.Count -eq 5 -and $last[1] -eq 'allocation') { $last[3] } else { 'NA' }
            processed_stage_missions = if ($last.Count -eq 5) { $last[4] } else { 'NA' }
            population_survival_rate = 'NA'
            search_space_reduction_percent = 'NA'
            general_states_explored = 'NA'
            stdout = "$id.stdout.txt"
            stderr = "$id.stderr.txt"
        }
        for ($i = 0; $i -lt $fields.Count; ++$i) {
            $row[$fields[$i]] = if ($status -eq 'complete') { $result[$i + 1] } else { 'NA' }
        }
        if ($includeQuality) {
            for ($i = 0; $i -lt $qualityFields.Count; ++$i) {
                $row[$qualityFields[$i]] = if ($status -eq 'complete') { $quality[$i + 1] } else { 'NA' }
            }
        }
        if ($includeLocal) {
            for ($i = 0; $i -lt $localFields.Count; ++$i) {
                $row[$localFields[$i]] = if ($status -ne 'complete') { 'NA' }
                    elseif ($mode -eq 'phase2') { $local[$i + 1] }
                    else { 0 }
            }
        }
        if ($includeCivilization) {
            for ($i = 0; $i -lt $civilizationFields.Count; ++$i) {
                $row[$civilizationFields[$i]] = if ($status -eq 'complete') { $civilization[$i + 1] } else { 'NA' }
            }
        }
        if ($AllocationMetrics) {
            $aidCargo = ($lines | Where-Object { $_ -like 'AID_CARGO,*' } | Select-Object -Last 1) -split ','
            $work = ($lines | Where-Object { $_ -like 'COORDINATED_AID,*' } | Select-Object -Last 1) -split ','
            $row['scheduled_aid_cargo'] = if ($aidCargo.Count -eq 3) { $aidCargo[1] } else {'NA'}
            $row['rejected_aid_missions'] = if ($aidCargo.Count -eq 3) { $aidCargo[2] } else {'NA'}
            $extra = @('allocation_attempts','released_attempts','partial_commitments','coordination_route_queries',
                'allocation_feasibility_checks','tentative_cargo','trial_committed_cargo','aid_candidate_retained')
            for ($i = 0; $i -lt $extra.Count; ++$i) {
                $row[$extra[$i]] = if ($work.Count -eq $extra.Count + 1) { $work[$i + 1] } else {'NA'}
            }
            $profile = ($lines | Where-Object { $_ -like 'AID_PROFILE,*' } | Select-Object -Last 1) -split ','
            $names = @('indexed_aid','itinerary_builds','ship_candidates','itinerary_setup_seconds',
                'recovery_seconds','aid_route_seconds','coordination_seconds','aid_replay_seconds')
            for ($i = 0; $i -lt $names.Count; ++$i) {
                $row[$names[$i]] = if ($profile.Count -eq $names.Count + 1) { $profile[$i + 1] } else {'NA'}
            }
            $planning = ($lines | Where-Object { $_ -like 'CIVILIZATION_PLANNING_SECONDS,*' } | Select-Object -Last 1) -split ','
            $row['civilization_planning_seconds'] = if ($planning.Count -eq 2) { $planning[1] } else {'NA'}
            $at = @{}
            foreach ($record in $lines | Where-Object { $_ -like 'PROGRESS,*' }) {
                $parts = $record -split ','
                if (!$at.ContainsKey($parts[1])) { $at[$parts[1]] = [double]$parts[2] }
            }
            foreach ($pair in @('generation,allocation','allocation,scheduling','scheduling,civilization-planning','simulation,complete')) {
                $from,$to = $pair -split ','
                $row["stage_${from}_seconds"] = if ($at.ContainsKey($from) -and $at.ContainsKey($to)) {
                    $at[$to] - $at[$from]
                } else { 'NA' }
            }
        }
        [pscustomobject]$row | Export-Csv -LiteralPath $csv -Append -NoTypeInformation
        $lines | Where-Object { $_ -notmatch '^(PROGRESS|RESULT|MEMORY|PLAN),' } | Write-Host
        Write-Host "$id -> $status (stage $($row.last_stage), peak $($row.peak_working_set_bytes) bytes)"
    }
}
