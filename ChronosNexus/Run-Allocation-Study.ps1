param(
    [Parameter(Mandatory)][string]$OriginalExecutable,
    [Parameter(Mandatory)][string]$Executable,
    [string]$OutputDirectory = "$PSScriptRoot\benchmark-results\runs\allocation-study",
    [string[]]$Variants = @('original', 'consolidated', 'competing-recipients', 'feasibility-aware')
)

$ErrorActionPreference = 'Stop'
$cases = @(
    @{Seed=20261119; Scenario='scarcity'; Sizes=@('small','medium')},
    @{Seed=20261120; Scenario='scarcity'; Sizes=@('medium')},
    @{Seed=20261120; Scenario='standard'; Sizes=@('small','medium','large')},
    @{Seed=20261124; Scenario='production-cut'; Sizes=@('small','medium','large')}
)
foreach ($variant in $Variants) {
    if ($variant -notin 'original','consolidated','competing-recipients','feasibility-aware') {
        throw "Unknown allocation study variant: $variant"
    }
    $folder = Join-Path $OutputDirectory $variant
    if (Test-Path (Join-Path $folder 'results.csv')) {
        throw "Choose a new output directory for $variant."
    }
}
foreach ($case in $cases) {
    foreach ($variant in $Variants) {
        $binary = if ($variant -eq 'original') { $OriginalExecutable } else { $Executable }
        $params = @{
            Executable=$binary; Sizes=$case.Sizes; Modes=@('phase3'); Seed=$case.Seed
            Scenario=$case.Scenario; TimeoutSeconds=35; MemoryMiB=1024
            OutputDirectory=(Join-Path $OutputDirectory $variant)
            AllocationMetrics=$true; TracePlan=($case.Scenario -eq 'scarcity')
        }
        if ($variant -eq 'competing-recipients') { $params.ExperimentalAllocator = $true }
        if ($variant -eq 'feasibility-aware') { $params.FeasibilityAwareAllocator = $true }
        & "$PSScriptRoot\Run-Benchmarks.ps1" @params
    }
}

$rows = foreach ($variant in $Variants) {
    foreach ($r in Import-Csv (Join-Path $OutputDirectory "$variant\results.csv")) {
        $rate = 'NA'
        if ($r.status -eq 'complete' -and ([int]$r.scheduled + [int]$r.rejected) -gt 0) {
            $rate = [int]$r.completed / ([int]$r.scheduled + [int]$r.rejected)
        }
        $inputScenario = if ($r.stdout -match '-phase3-(standard|scarcity|production-cut)-') { $Matches[1] }
            else { $r.scenario }
        $r | Select-Object @{n='variant';e={$variant}}, @{n='input_scenario';e={$inputScenario}},
            @{n='completion_rate';e={$rate}}, *
    }
}
$rows | Export-Csv -NoTypeInformation (Join-Path $OutputDirectory 'paired.csv')
