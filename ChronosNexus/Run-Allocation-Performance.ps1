param(
    [Parameter(Mandatory)][string]$Executable,
    [string]$OutputDirectory = "$PSScriptRoot\benchmark-results\runs\allocation-performance",
    [switch]$Diagnostic
)
$ErrorActionPreference = 'Stop'
if (Test-Path $OutputDirectory) { throw 'Choose a fresh output directory.' }
$cases = if ($Diagnostic) {
    @(@{Sizes=@('large'); Seed=20261120; Scenario='standard'; Repeat=1})
} else {
    @(
        @{Sizes=@('small'); Seed=20261119; Scenario='scarcity'; Repeat=3},
        @{Sizes=@('medium'); Seed=20261120; Scenario='scarcity'; Repeat=3},
        @{Sizes=@('small','medium','large'); Seed=20261120; Scenario='standard'; Repeat=1},
        @{Sizes=@('small','medium','large'); Seed=20261125; Scenario='production-cut'; Repeat=1}
    )
}
$variants = @('consolidated','legacy-aid','indexed-aid')
if ($Diagnostic) { $variants = @('legacy-aid','indexed-aid') }
foreach ($case in $cases) {
    for ($sample=1; $sample -le $case.Repeat; ++$sample) {
        $order = if ($sample % 2) { $variants } else { @($variants | Sort-Object -Descending) }
        foreach ($variant in $order) {
            $params = @{
                Executable=$Executable; Sizes=$case.Sizes; Modes=@('phase3'); Seed=$case.Seed
                Scenario=$case.Scenario; TimeoutSeconds=$(if ($Diagnostic) { 120 } else { 35 })
                MemoryMiB=1024; OutputDirectory=(Join-Path $OutputDirectory "$variant\$($case.Scenario)-$($case.Seed)-$sample")
                AllocationMetrics=$true; TracePlan=$true
            }
            if ($variant -ne 'consolidated') { $params.FeasibilityAwareAllocator = $true }
            if ($variant -eq 'legacy-aid') { $params.LegacyAidSearch = $true }
            & "$PSScriptRoot\Run-Benchmarks.ps1" @params
        }
    }
}
$rows = foreach ($variant in $variants) {
    foreach ($file in Get-ChildItem (Join-Path $OutputDirectory $variant) -Recurse -Filter results.csv) {
        foreach ($row in Import-Csv $file.FullName) {
            $path = Join-Path $file.DirectoryName $row.stdout
            $trace = @(Get-Content $path | Where-Object { $_ -match '^(PLAN|AID_TRACE|CIVILIZATION),' })
            $digest = if ($row.status -eq 'complete') {
                $hash = [Security.Cryptography.SHA256]::Create()
                try {
                    [BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes(($trace -join "`n")))).Replace('-','')
                } finally { $hash.Dispose() }
            } else { 'NA' }
            $group = $file.Directory.Name
            $row | Select-Object @{n='variant';e={$variant}}, @{n='input_case';e={$group}},
                @{n='decision_trace_sha256';e={$digest}}, * -ExcludeProperty stdout,stderr
        }
    }
}
$rows | Export-Csv -NoTypeInformation (Join-Path $OutputDirectory 'paired.csv')
