param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $Executable).Path
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a fresh output directory.' }
$output = (New-Item -ItemType Directory -Path $OutputDirectory).FullName
$cases = @(
    @('small', 20261129, 'standard',       @(1, 25, 200, 2500, 2500)),
    @('small', 20261130, 'scarcity',       @(25, 2500)),
    @('small', 20261201, 'dynamic',        @(25, 2500)),
    @('medium', 20261129, 'standard',      @(100, 750, 5000, 5000)),
    @('medium', 20261130, 'scarcity',      @(100, 1000, 5000)),
    @('medium', 20261202, 'production-cut',@(100, 1000, 5000)),
    @('medium', 20261201, 'dynamic',       @(100, 1000, 5000)),
    @('large', 20261120, 'standard',       @(35000, 45000, 45000)),
    @('large', 20261203, 'dynamic',        @(35000, 45000))
)
$dimensions = @{small = @(100, 500, 50); medium = @(1000, 10000, 500); large = @(10000, 100000, 5000)}
$summaryFields = 'feasible','planning_complete','simulation_complete','first_feasible_s','first_nonempty_s',
    'evaluated','improved','interrupted','validation_s','reserve_s','engine_elapsed_s','engine_peak_bytes',
    'exhaustion','exhaustion_stage','engine_overshoot_s'
$metricFields = 'legacy_score','expanded_score','completed','failed','timely_service','critical_shortage',
    'total_shortage','unserved_consumption','transportation_cost','fuel','waste','risk','events_applied'
$traceFields = 'seconds','legacy_score','expanded_score','scheduled','failed','unscheduled_external',
    'timely_service','critical_shortage','total_shortage','unserved_consumption','transportation_cost','fuel','waste','risk'
function Add-Fields($Row, $Keys, $Values) {
    for ($i = 0; $i -lt $Keys.Count; ++$i) {
        $Row[$Keys[$i]] = if ($null -ne $Values -and $i -lt $Values.Count) { $Values[$i] } else { 'NA' }
    }
}
function Read-Record($Lines, $Kind) {
    $line = $Lines | Where-Object { $_.StartsWith($Kind + ',') } | Select-Object -Last 1
    if ($line) { ($line -split ',') | Select-Object -Skip 1 }
}
$run = 0
foreach ($case in $cases) {
    $size, $seed, $scenario, $budgets = $case
    $variants = ,@('legacy', $false, $false)
    if ($scenario -eq 'scarcity' -or ($size -eq 'medium' -and $scenario -eq 'dynamic')) {
        $variants += ,@('expanded', $true, $false)
        $variants += ,@('expanded-indexed-aid', $true, $true)
    }
    foreach ($variant in $variants) {
        $variantName, $expanded, $aid = $variant
        $times = if ($variantName -eq 'legacy') { @(0) + $budgets } else { @(0, $budgets[-1]) }
        foreach ($milliseconds in $times) {
            $run++
            $stem = Join-Path $output ('{0:D2}-{1}-{2}-{3}-{4}' -f $run,$size,$scenario,$variantName,$milliseconds)
            $argsList = @('--benchmark', $size, "$seed", 'phase3', "--scenario=$scenario")
            if ($expanded) { $argsList += '--expanded-objective' }
            if ($aid) { $argsList += '--feasibility-aware-allocator', '--reverse-aid-bounds' }
            if ($milliseconds) { $argsList += "--time-budget-ms=$milliseconds", '--memory-budget-mib=1024' }
            $cap = if ($size -eq 'large') { 50 } else { 15 }
            $status = 'returned'
            $watch = [Diagnostics.Stopwatch]::StartNew()
            Write-Host "Run $run $size $scenario $variantName $milliseconds ms"
            $process = Start-Process -FilePath $exe -ArgumentList $argsList -RedirectStandardOutput "$stem.stdout.txt" `
                -RedirectStandardError "$stem.stderr.txt" -PassThru -WindowStyle Hidden
            $null = $process.Handle
            $peak = 0L
            try {
                while (-not $process.WaitForExit(10)) {
                    $process.Refresh()
                    try { $peak = [Math]::Max($peak, $process.PeakWorkingSet64) } catch [InvalidOperationException] {}
                    if ($watch.Elapsed.TotalSeconds -ge $cap) { $status = 'external-timeout'; $process.Kill(); break }
                    if ($peak -ge 1GB) { $status = 'external-memory'; $process.Kill(); break }
                }
                $process.WaitForExit()
                if ($status -eq 'returned' -and $process.ExitCode -ne 0) { $status = 'failed' }
                $wall = $watch.Elapsed.TotalSeconds
            } finally {
                $process.Dispose()
            }
            $lines = Get-Content -LiteralPath "$stem.stdout.txt"
            $key = [ordered]@{run=$run; size=$size; seed=$seed; scenario=$scenario; variant=$variantName; budget_ms=$milliseconds}
            $row = [ordered]@{} + $key
            $row['status'] = $status
            $row['planets'], $row['routes'], $row['input_missions'] = $dimensions[$size]
            $row['wall_s'] = $wall
            $row['sampled_peak_bytes'] = $peak
            $row['external_limit_s'] = $cap
            $row['working_set_limit_mib'] = 1024
            $row['external_overshoot_s'] = if ($milliseconds) { [Math]::Max(0.0, $wall - $milliseconds / 1000) } else { 'NA' }
            Add-Fields $row $summaryFields (Read-Record $lines 'ANYTIME')
            if ($milliseconds) {
                Add-Fields $row $metricFields (Read-Record $lines 'ANYTIME_OUTCOME')
                $samples = @($lines | Where-Object { $_.StartsWith('INCUMBENT,') })
                $last = $null
                foreach ($line in $samples) {
                    $sample = [ordered]@{} + $key
                    Add-Fields $sample $traceFields (($line -split ',') | Select-Object -Skip 1)
                    [pscustomobject]$sample | Export-Csv -NoTypeInformation -Append (Join-Path $output 'incumbents.csv')
                    $last = $sample
                }
                $row['unscheduled_external'] = if ($null -ne $last) { $last['unscheduled_external'] } else { $dimensions[$size][2] }
                $row['final_nominal_scheduled'] = if ($null -ne $last) { $last['scheduled'] } else { 'NA' }
            } else {
                $result = Read-Record $lines 'RESULT'
                $civilization = Read-Record $lines 'CIVILIZATION'
                $pair = Read-Record $lines 'OBJECTIVE_PAIR'
                $metrics = if ($result -and $pair -and $civilization) {
                    @($pair[0], $pair[1], $result[3], $result[4], $civilization[5], $civilization[10],
                        $result[6], $result[7], $result[11], $result[10], $result[8], $result[12], $result[16])
                }
                Add-Fields $row $metricFields $metrics
                if ($metrics) {
                    $row['feasible'] = '1'
                    $row['planning_complete'] = '1'
                    $row['simulation_complete'] = '1'
                    $row['engine_elapsed_s'] = $result[25]
                    $row['engine_peak_bytes'] = @(Read-Record $lines 'MEMORY')[0]
                    $row['evaluated'] = $civilization[12]
                }
                $row['unscheduled_external'] = 'NA'
                $row['final_nominal_scheduled'] = if ($result) { $result[1] } else { 'NA' }
            }
            [pscustomobject]$row | Export-Csv -NoTypeInformation -Append (Join-Path $output 'anytime.csv')
            if ($status -eq 'failed') { Write-Warning (Get-Content -LiteralPath "$stem.stderr.txt" -Raw) }
        }
    }
}
