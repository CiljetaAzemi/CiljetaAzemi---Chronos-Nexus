[CmdletBinding()]
param(
    [string]$Executable = "$PSScriptRoot\x64\Release\ChronosNexus.exe",
    [ValidateSet('small', 'medium', 'large', 'extreme')]
    [string[]]$Sizes = @('small', 'medium', 'large', 'extreme'),
    [uint32]$Seed = 20261118,
    [ValidateSet('custom', 'earliest', 'basic', 'all-routes', 'greedy-build', 'stress', 'stress-all-routes')]
    [string[]]$Modes = @('custom'),
    [ValidateRange(1, 86400)]
    [int]$TimeoutSeconds = 600,
    [ValidateRange(64, 1048576)]
    [int]$MemoryMiB = 1024,
    [string]$OutputDirectory = "$PSScriptRoot\benchmark-results"
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
$dimensions = @{
    small = 100, 500, 50
    medium = 1000, 10000, 500
    large = 10000, 100000, 5000
    extreme = 100000, 1000000, 50000
}
foreach ($size in $Sizes) {
    foreach ($mode in $Modes) {
        $id = "$size-$Seed-$mode-$([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))"
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
            $process = Start-Process -FilePath $exe -ArgumentList @('--benchmark', $size, "$Seed", $mode) `
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
        if ($status -eq 'complete' -and $result.Count -ne ($fields.Count + 1)) {
            $status = 'missing_result'
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
        [pscustomobject]$row | Export-Csv -LiteralPath $csv -Append -NoTypeInformation
        $lines | Where-Object { $_ -notmatch '^(PROGRESS|RESULT|MEMORY),' } | Write-Host
        Write-Host "$id -> $status (stage $($row.last_stage), peak $($row.peak_working_set_bytes) bytes)"
    }
}
