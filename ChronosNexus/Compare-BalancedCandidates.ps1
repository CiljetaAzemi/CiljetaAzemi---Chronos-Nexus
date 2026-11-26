param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$OutputCsv
)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path $Executable).Path
$fields = 'legacy','expanded','coverage','critical_unserved','critical_ending','service','ending','unserved',
    'transport','fuel','waste','risk','construction','scheduled','coverage_band','critical_unserved_band',
    'critical_ending_band','service_band','expanded_tie'
$rows = foreach ($case in @(@(20261127,'scarcity'),@(20261201,'dynamic'))) {
    $seed,$scenario = $case
    $lines = & $exe --benchmark medium "$seed" phase3 "--scenario=$scenario" --security-priority --trace-candidates
    if ($LASTEXITCODE) { throw "Candidate enumeration failed for $scenario." }
    $id = 0
    $pool = foreach ($line in $lines) {
        if (!$line.StartsWith('VALIDATED_CANDIDATE,')) { continue }
        $values = ($line -split ',') | Select-Object -Skip 1
        if ($values.Count -ne $fields.Count) { throw 'Unexpected candidate telemetry.' }
        $row = [ordered]@{seed=$seed; scenario=$scenario; candidate=(++$id)}
        for ($i = 0; $i -lt $fields.Count; ++$i) {
            $row[$fields[$i]] = [double]::Parse($values[$i], [Globalization.CultureInfo]::InvariantCulture)
        }
        [pscustomobject]$row
    }
    if (!$pool) { throw "No validated candidates for $scenario." }
    $strict = $pool | Sort-Object -Stable {-$_.coverage},critical_unserved,critical_ending,{-$_.service},{-$_.expanded} | Select-Object -First 1
    $legacy = $pool | Sort-Object -Stable legacy -Descending | Select-Object -First 1
    $expanded = $pool | Sort-Object -Stable expanded -Descending | Select-Object -First 1
    $balanced = $pool | Sort-Object -Stable coverage_band,critical_unserved_band,critical_ending_band,service_band,expanded_tie -Descending | Select-Object -First 1
    # Preserve strict security and legacy service if jointly feasible. The strict
    # plan's own service is the fallback floor, making the reference admissible.
    $floor = [Math]::Min($legacy.service, $strict.service)
    $constrained = $pool | Where-Object {
        $_.coverage -ge $strict.coverage -and $_.critical_unserved -le $strict.critical_unserved -and
        $_.critical_ending -le $strict.critical_ending -and $_.service -ge $floor
    } | Sort-Object -Stable fuel,transport,waste,risk | Select-Object -First 1
    $benefits = 'coverage','service'
    $burdens = 'critical_unserved','critical_ending','ending','unserved','transport','fuel','waste','risk','construction'
    foreach ($a in $pool) {
        $dominated = $false
        foreach ($b in $pool) {
            if ($a.candidate -eq $b.candidate) { continue }
            $worse = @($benefits | Where-Object { $b.$_ -lt $a.$_ }) +
                @($burdens | Where-Object { $b.$_ -gt $a.$_ })
            $better = @($benefits | Where-Object { $b.$_ -gt $a.$_ }) +
                @($burdens | Where-Object { $b.$_ -lt $a.$_ })
            if (!$worse.Count -and $better.Count) { $dominated = $true; break }
        }
        $a | Select-Object *,@{n='nondominated';e={!$dominated}},
            @{n='legacy_selected';e={$a.candidate -eq $legacy.candidate}},
            @{n='expanded_selected';e={$a.candidate -eq $expanded.candidate}},
            @{n='strict_selected';e={$a.candidate -eq $strict.candidate}},
            @{n='balanced_selected';e={$a.candidate -eq $balanced.candidate}},
            @{n='constraint_selected';e={$a.candidate -eq $constrained.candidate}}
    }
}
$rows | Export-Csv -NoTypeInformation -LiteralPath $OutputCsv
