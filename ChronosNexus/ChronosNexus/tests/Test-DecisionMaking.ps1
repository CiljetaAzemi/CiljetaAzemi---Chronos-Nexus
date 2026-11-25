param(
    [Parameter(Mandatory)][string]$Executable,
    [string]$Baseline,
    [string]$OutputDirectory = (Join-Path ([IO.Path]::GetTempPath()) ('chronos-decisions-' + [Guid]::NewGuid()))
)

$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if ($Baseline) { $Baseline = (Resolve-Path -LiteralPath $Baseline).Path }
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path

function Route($id, $from, $to, $distance, $time, $fuel) {
    @{ id=$id; from=$from; to=$to; distance=$distance; travel_time=$time; fuel=$fuel; capacity=10 }
}
function Mission($id, $from, $to, $amount, $deadline, $priority) {
    @{ id=$id; origin=$from; destination=$to; resource='food'; amount=$amount; deadline=$deadline; priority=$priority }
}
function Scenario {
    @{
        version=1; budget=0; horizon=8
        locations=@(1..4 | ForEach-Object { @{ id=$_; type='colony' } })
        routes=@((Route 10 1 2 100 2 10), (Route 11 1 3 1 1 1), (Route 12 3 4 1 1 1))
        inventory=@(1..4 | ForEach-Object { @{ location=$_; resource='food'; stock=0; target=0; storage=20 } })
        spacecraft=@(@{ id=1; location=1; cargo_capacity=10; fuel_capacity=10; fuel=10; speed=1; operating_cost=1 })
        missions=@((Mission 1 1 2 5 4 9), (Mission 2 1 3 5 1 5), (Mission 3 3 4 5 2 5))
        events=@()
    }
}
function Run-Case($name, $scenario, $verify) {
    $scenario.inventory[0].stock=5
    $inputPath = Join-Path $OutputDirectory "$name.json"
    $scenario | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $inputPath -Encoding utf8
    foreach ($version in @('before', 'after')) {
        $binary = if ($version -eq 'before') { $Baseline } else { $Executable }
        if (!$binary) { continue }
        $outputPath = Join-Path $OutputDirectory "$name-$version.json"
        $arguments = '--scenario-file "' + $inputPath + '" --output "' + $outputPath + '"'
        $process = Start-Process -FilePath $binary -PassThru -WindowStyle Hidden -ArgumentList $arguments
        $peak = 0
        $watch = [Diagnostics.Stopwatch]::StartNew()
        while (!$process.HasExited) {
            try { $peak = [Math]::Max($peak, $process.PeakWorkingSet64) } catch [InvalidOperationException] {}
            if ($watch.Elapsed.TotalSeconds -gt 30) {
                $process.Kill()
                throw "$name timed out"
            }
            $process.Refresh()
        }
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) { throw "$name exited with $($process.ExitCode)" }
        $result = Get-Content -LiteralPath $outputPath -Raw | ConvertFrom-Json
        if ($version -eq 'after' -and !(& $verify $result)) { throw "$name failed its regression" }
        $actual = $result.simulation
        [pscustomobject]@{
            Case=$name; Version=$version; Score=$actual.civilization.score
            Completed=$actual.completed; Failed=$actual.failed; Cost=$actual.transportation_cost
            Fuel=$actual.fuel; Shortage=$actual.remaining_shortage; Waste=$actual.waste; Risk=$actual.risk
            EngineSeconds=$result.processing_seconds
            ObservedPeakBytes=if ($peak -gt 0) { $peak } else { $null }
            Assignments=($result.planned.assignments.mission_id -join ',')
            Ships=($result.planned.assignments.spacecraft_id -join ',')
        }
    }
}

$s=Scenario
$s.missions=@($s.missions[0], $s.missions[1]); $s.missions[0].amount=2
Run-Case 'priority' $s { param($r) $r.planned.assignments[0].mission_id -eq 2 }

$s=Scenario
$s.routes[0]=Route 10 1 2 1 1 1
$s.missions=@((Mission 1 1 2 5 2 5), (Mission 2 1 3 5 2 5)); $s.missions[1].importance=2
Run-Case 'importance' $s { param($r) $r.planned.assignments[0].mission_id -eq 2 }

$s=Scenario
$s.missions=@($s.missions[0])
$s.spacecraft+=@{ id=2; location=1; cargo_capacity=10; fuel_capacity=10; fuel=10; speed=1; operating_cost=0 }
Run-Case 'spacecraft-cost' $s { param($r) $r.planned.assignments[0].spacecraft_id -eq 2 }

Run-Case 'opportunity-chain' (Scenario) { param($r)
    $r.simulation.completed -eq 2 -and ($r.planned.assignments.mission_id -join ',') -eq '2,3'
}

$s=Scenario
$s.missions[0].priority=30
Run-Case 'retain-valuable-expensive' $s { param($r) $r.planned.assignments[0].mission_id -eq 1 }

$s=Scenario
$s.missions=@($s.missions[0])
$s.events=@(@{ id=1; time=0; type='urgent_mission'; mission=(Mission 8 1 3 5 1 100) })
Run-Case 'urgent-displacement' $s { param($r)
    ($r.simulation.missions | Where-Object mission_id -eq 8).status -eq 'completed' -and
    $r.simulation.replanning_decisions.reason -contains 'displaced_booking'
}

$s=Scenario
$s.missions=@($s.missions[1]); $s.routes+=,(Route 14 1 3 2 1 1)
$s.events=@(@{ id=1; time=0; type='route_risk_increase'; target=11; severity=1 })
Run-Case 'risk-increase' $s { param($r)
    $r.simulation.risk -eq 0 -and $r.simulation.replanning_decisions.reason -contains 'increased_route_risk'
}

$s=Scenario
$s.missions=@($s.missions[0], $s.missions[1]); $s.missions[0].priority=30; $s.missions[1].importance=100
Run-Case 'objective-selection' $s { param($r) $r.planned.assignments[0].mission_id -eq 1 }

$s=Scenario
$s.missions=@()
foreach ($account in $s.inventory) { $account.aid=$true }
$s.inventory[1].target=5; $s.inventory[1].priority=9
$s.inventory[2].target=5; $s.inventory[2].priority=5
Run-Case 'aid-default' $s { param($r)
    !$r.experimental_allocator -and $r.planned.allocation_proposals[0].destination -eq 2
}
$s.experimental_allocator=$true
Run-Case 'aid-competition' $s { param($r)
    $r.experimental_allocator -and $r.planned.allocation_proposals[0].destination -eq 3 -and
    $r.simulation.remaining_shortage -eq 5
}
