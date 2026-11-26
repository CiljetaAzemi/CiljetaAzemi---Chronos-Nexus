param(
    [Parameter(Mandatory)][string]$Executable,
    [string]$OutputDirectory = "$PSScriptRoot\benchmark-results\runs\objective-study"
)
$ErrorActionPreference = 'Stop'
if (Test-Path $OutputDirectory) { throw 'Choose a fresh output directory.' }
$variants = @('legacy','expanded','expanded-aid')
$cases = @(
    @{Sizes=@('small','medium','large'); Seed=20261120; Scenario='standard'; Profile='balanced'},
    @{Sizes=@('small','medium'); Seed=20261127; Scenario='scarcity'; Profile='balanced'},
    @{Sizes=@('small','medium'); Seed=20261128; Scenario='production-cut'; Profile='balanced'},
    @{Sizes=@('medium'); Seed=20261127; Scenario='scarcity'; Profile='security'},
    @{Sizes=@('medium'); Seed=20261127; Scenario='scarcity'; Profile='service'}
)
foreach ($case in $cases) {
    foreach ($variant in $variants) {
        if ($case.Profile -ne 'balanced' -and $variant -ne 'expanded') { continue }
        $folder = Join-Path $OutputDirectory "$variant\$($case.Scenario)-$($case.Seed)-$($case.Profile)"
        $params = @{
            Executable=$Executable; Sizes=$case.Sizes; Modes=@('phase3'); Seed=$case.Seed
            Scenario=$case.Scenario; ObjectiveProfile=$case.Profile; TimeoutSeconds=45
            MemoryMiB=1024; OutputDirectory=$folder; ObjectiveMetrics=$true
        }
        if ($variant -ne 'legacy') { $params.ExpandedObjective = $true }
        if ($variant -eq 'expanded-aid') {
            $params.FeasibilityAwareAllocator = $true
            $params.ReverseAidBounds = $true
        }
        & "$PSScriptRoot\Run-Benchmarks.ps1" @params
    }
}
$rows = foreach ($variant in $variants) {
    foreach ($csv in Get-ChildItem (Join-Path $OutputDirectory $variant) -Recurse -Filter results.csv) {
        $inputCase = $csv.Directory.Name
        Import-Csv $csv.FullName | Select-Object @{n='variant';e={$variant}},@{n='input_case';e={$inputCase}},* -ExcludeProperty stdout,stderr
    }
}
$rows | Export-Csv -NoTypeInformation (Join-Path $OutputDirectory 'paired.csv')
