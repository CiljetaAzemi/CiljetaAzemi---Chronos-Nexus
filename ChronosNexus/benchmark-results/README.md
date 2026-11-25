# Experimental evaluation and reproducibility

## Research questions

The evaluation examines scaling under fixed resource budgets, the effect of ship ranking on mission completion, the effect of routing information on delivered cargo, the strategic value of infrastructure selection, and the cost of conditional route recomputation. The algorithms and experimental score are defined in the [technical report](../../README.md).

## Methodology

The experiments used Visual Studio MSBuild 18.11, MSVC v145, and Release x64. Each configuration ran in a separate process with seed `20261118`. The generator uses `std::mt19937` and creates regional routes, long-range wormholes, colonies, stations, distinct food and medicine inventories, and seeded disruptions. The ordinary scenarios include route closures, ship losses, and production decreases. Inventories are evaluated through day 30. Aid missions are generated in addition to the input mission count.

| Dataset | Planets | Directed routes | Input missions |
|---|---:|---:|---:|
| Small | 100 | 500 | 50 |
| Medium | 1,000 | 10,000 | 500 |
| Large | 10,000 | 100,000 | 5,000 |
| Extreme | 100,000 | 1,000,000 | 50,000 |

| Mode | Treatment |
|---|---|
| `custom` | Weighted ship ranking, route-aware allocation, conditional route repair, and Knapsack construction. |
| `earliest` | Earliest feasible arrival replaces weighted ship ranking. Mission ordering and feasibility checks are shared. |
| `basic` | Allocation ignores routing cost and reachability. Subsequent scheduling and execution use the same feasibility checks. |
| `all-routes` | Every eligible departure route is recomputed. Fleet assignments and inventory plans are fixed. |
| `greedy-build` | Construction is selected by value per credit under the same budget. |
| `stress`, `stress-all-routes` | Conditional and unconditional recomputation are compared under the same 33 additional edge closures in the recorded medium pair. Closures occur at selected scheduled departures. |

The standard resource limits were 600 seconds and 1,024 MiB of peak working set. Engine time measures the benchmark internally. Runner wall time includes startup and polling. Completed runs in the comparison suite report in-process peak working set. The timed-out run reports the highest peak observed by the controller. Population survival, general state-space exploration, and search-space reduction percentage are recorded as `NA`.

## Scaling results

Completed rows are from the [comparison dataset](follow-up/results.csv). The extreme row is from the [initial scaling dataset](results.csv). Times for completed rows are engine times. The timeout row reports wall time.

| Size | Status | Time (s) | Peak working set (MiB) | Scheduled / rejected | Completed / failed | Delivered | Ending shortage | Score |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Small | Completed | 0.013 | 5.18 | 30 / 25 | 22 / 8 | 488 | 193.27 | 345.79 |
| Medium | Completed | 0.207 | 10.24 | 273 / 277 | 213 / 60 | 5,430.24 | 2,088.60 | 2,816.80 |
| Large | Completed | 10.813 | 52.26 | 2,605 / 2,897 | 2,150 / 455 | 55,468.20 | 20,232 | 33,893.70 |
| Extreme | Timed out | 600.238 | 409.12 | NA | NA | NA | NA | NA |

Large spent approximately 10.64 seconds in scheduling and expanded 11,074,447 graph nodes overall. Extreme finished generation and allocation. Its last checkpoint recorded 13,944 mission records evaluated during scheduling at 599.526 seconds. Simulation outcomes were unavailable. A separate [memory-limit experiment](limit-check/results.csv) terminated the extreme configuration during generation with a 64 MiB threshold and a reported peak of 101,482,496 bytes. The threshold is checked by polling and can be exceeded before termination.

## Algorithm comparisons

Percentage change is calculated relative to the comparison mode, as 100 × (custom − comparison) / comparison. Values are calculated from the recorded CSV fields.

| Metric and comparison | Small | Medium | Large |
|---|---:|---:|---:|
| Completed missions, custom vs. earliest arrival | 22 vs. 22 (0.00%) | 213 vs. 209 (+1.91%) | 2,150 vs. 2,138 (+0.56%) |
| Delivered quantity, route-aware vs. basic allocation | 488 vs. 455 (+7.25%) | 5,430.24 vs. 4,835.34 (+12.30%) | 55,468.20 vs. 49,083.50 (+13.01%) |
| Construction strategic value, Knapsack vs. greedy | 135 vs. 103 (+31.07%) | 238 vs. 238 (0.00%) | 3,495 vs. 3,490 (+0.14%) |

Earliest arrival consumed less fuel on large (91,596.2 versus 92,379.2 units). On large, greedy construction produced a slightly higher overall score (33,898.0 versus 33,893.7) despite its lower strategic value, with an ending shortage of 20,222.7 versus 20,232. These outcomes illustrate differences between a component objective and the system-level score. [Comparison data](follow-up/results.csv)

| Medium closure stress metric | Conditional | Every eligible route | Change |
|---|---:|---:|---:|
| Route recomputations | 35 | 215 | 83.72% fewer |
| Time spent in route recomputation | 8.257 ms | 12.915 ms | 36.07% lower |
| Total engine time | 0.110193 s | 0.102824 s | 7.17% higher |
| Completed / failed missions | 207 / 66 | 207 / 66 | Equal |
| Delivered quantity | 5,164.64 | 5,164.64 | Equal |

Conditional repair changed 29 routes, as did unconditional recomputation. The ordinary custom scenarios performed zero route recomputations. The stress comparison measures route repair with fixed assignments. [Stress data](follow-up/results.csv)

## Reproduction

Build Release x64 using the [build guide](../BUILD.md). From the repository root in Developer PowerShell, specify a fresh output directory for each run to keep the archived CSVs separate. The runner accepts `-Executable` if the executable is outside its default path.

```powershell
.\ChronosNexus\Run-Benchmarks.ps1 -Sizes small,medium,large,extreme -Seed 20261118 -Modes custom -TimeoutSeconds 600 -MemoryMiB 1024 -OutputDirectory .\x64\reproduction\scaling
.\ChronosNexus\Run-Benchmarks.ps1 -Sizes small,medium,large -Seed 20261118 -Modes custom,earliest,basic,all-routes,greedy-build -OutputDirectory .\x64\reproduction\comparison
.\ChronosNexus\Run-Benchmarks.ps1 -Sizes medium -Seed 20261118 -Modes stress,stress-all-routes -OutputDirectory .\x64\reproduction\stress
.\ChronosNexus\Run-Benchmarks.ps1 -Sizes extreme -Seed 20261118 -Modes custom -MemoryMiB 64 -OutputDirectory .\x64\reproduction\memory-limit
```

The [runner](../Run-Benchmarks.ps1) writes CSV rows, progress markers, and per-run standard output and error files. Its default output is the ignored `runs/` directory inside `benchmark-results/`. Published result CSVs, stress summaries, equivalence digests, and regression summaries are kept in the experiment directories. Log filenames in the CSV identify outputs available after reproducing a run. Use `-TracePlan` to retain exact decisions in those local logs.

## Limitations

These are single-seed, single-run observations. Timing differences, especially at subsecond durations, do not establish repeatable speedups. Ordinary synthetic disruptions had limited impact on selected routes. The stress case intentionally targets selected edges and does not measure fleet-wide or inventory replanning. The workload does not establish performance on unseen competition scenarios, and the reported score uses the experimental weights defined in the technical report.

## Inventory-aware route-cache evaluation

The opt-in `inventory` mode uses bounded forward-route caching. The `inventory-uncached` control runs the same inventory-aware scheduler without route reuse. The comparison uses paired scenarios with seeds `20261118`, `42`, `1729`, `8675309`, and `314159` on small and medium, and the first three seeds on large. The solver records exact assignments, rejections, and recoveries after the engine-time and peak-memory measurements. The [paired measurements](inventory-cache/results.csv) and [equivalence checks](inventory-cache/equivalence.csv) record the comparison.

All 13 pairs had identical decision traces and simulation outcomes. On large, mean engine time decreased from 25.61 s to 14.90 s, and mean graph expansions decreased from 22.19 million to 12.51 million. Mean peak working set increased from 58.43 MiB to 60.94 MiB. These are single paired observations per seed.

Run the following from the repository root to reproduce the paired matrix.

```powershell
foreach ($seed in 20261118, 42, 1729, 8675309, 314159) {
    $sizes = if ($seed -in 20261118, 42, 1729) { 'small', 'medium', 'large' } else { 'small', 'medium' }
    .\ChronosNexus\Run-Benchmarks.ps1 -Sizes $sizes -Seed $seed -Modes inventory-uncached,inventory -TracePlan -TimeoutSeconds 120 -OutputDirectory .\x64\reproduction\inventory-cache
}
```

Inventory-aware planning remains opt-in. In the preceding [scarcity stress comparison](inventory-robustness/stress-results.csv), reducing initial stock to 40% and retaining 139 ships increased timely fulfillment from 5.99% to 9.84% under inventory-aware scheduling, while ending shortage rose from 5,410 to 5,623.52 units. Exact route reuse preserves the policy's decisions, including this trade-off. Future evaluations should consider timely fulfillment, ending shortage and fuel jointly, especially when additional departures compete with suppliers' retained stock.

## Experimental Phase 2 evaluation

The [local-search algorithm note](../PHASE2.md) specifies the deterministic budgets, feasibility checks, and nominal objective. Comparisons use the same fixed obligations and generated event traces for `custom`, `inventory`, and `phase2`. The [paired results](phase2-final/results.csv), [stress results](phase2-final/stress.csv), and [regression output](phase2-final/self-test.txt) record the runs.

```powershell
foreach ($seed in 20261118, 42, 1729, 8675309, 314159) {
    $sizes = if ($seed -in 20261118, 42, 1729) { 'small', 'medium', 'large' } else { 'small', 'medium' }
    .\ChronosNexus\Run-Benchmarks.ps1 -Sizes $sizes -Seed $seed -Modes custom,inventory,phase2 -TimeoutSeconds 120 -OutputDirectory .\x64\reproduction\phase2
}
```

Run `--self-test` for the exact tiny and medium stress fixtures. The local counters report attempted and accepted moves, recovered obligations, examined route arcs, and arcs pruned by eligibility, resource bounds, dominance, or the active-label cap. The ratio of pruned to examined arcs describes these queries. It does not estimate the size of the full combinatorial search space.

All 39 Release x64 runs completed across 13 three-mode groups. The two controls reproduced their archived solution outcomes. Phase 2 accepted 56 of 1,908 trials and pruned 1,093,361 of 1,435,488 examined arcs (76.17%). It recovered one previously rejected mission, displacing another. Completion counts matched Phase 1 in every pair.

Mean timely fulfillment relative to Phase 1 changed by −0.33 percentage points on small, +0.09 on medium, and zero on large. The small continuity seed increased realized ending shortage by 36 units despite passing the nominal shortage guard. The scarcity and donor-cut stress cases matched Phase 1 on completion, timely fulfillment, and shortage. In particular, the scarcity case still left higher ending shortage than the original baseline. The results support constrained routing and dependency recovery on the exact fixtures without establishing a general improvement over Phase 1.

## Experimental Phase 3 evaluation

The [technical report](../../README.md#experimental-civilization-objective) defines the objective, normalization, and planner. The four modes receive the same scenario and obligations in each group and all use the same scoring profile. Production-cut events are visible only to the final simulator. Construction effects from the chosen plan are included in the nominal comparison.

```powershell
$common = @{
    Modes = 'custom', 'inventory', 'phase2', 'phase3'
    TimeoutSeconds = 180
    OutputDirectory = '.\x64\reproduction\phase3'
}
.\ChronosNexus\Run-Benchmarks.ps1 @common -Sizes small,medium,large -Seed 20261118
.\ChronosNexus\Run-Benchmarks.ps1 @common -Sizes small,medium -Seed 42
.\ChronosNexus\Run-Benchmarks.ps1 @common -Sizes medium -Seed 42 -Scenario scarcity
.\ChronosNexus\Run-Benchmarks.ps1 @common -Sizes medium -Seed 42 -Scenario production-cut
foreach ($profile in 'security', 'service') {
    .\ChronosNexus\Run-Benchmarks.ps1 @common -Sizes medium -Seed 42 -Scenario scarcity -ObjectiveProfile $profile
}
```

The CSV retains the historical score and adds the normalized objective, its components, critical-colony ending shortage and unserved demand, and nominal candidate counts. Completion and execution-failure rates use the fixed total of scheduled plus rejected obligations as the denominator. The existing weighted-cargo columns report all-obligation fulfillment. Engine time includes the extra planning and nominal replays. The historical graph counter describes the selected scheduling path and does not sum discarded Phase 3 proposals. The [run evidence](phase3/results.csv) and [regression output](phase3/self-test.txt) record this evaluation.

On the balanced continuity cases, Phase 3 scored 54.165 on small and 55.168 on medium, versus 54.095 and 55.048 for cached Phase 1. Small completed one fewer obligation and incurred more unserved demand. Medium completed two extra obligations and reduced unserved demand by 33 units, while ending shortage increased by six. On large, Phase 3 matched Phase 2 on measured solution outcomes but took 69.64 seconds versus 24.42 seconds.

Scarcity exposed a policy trade-off. The balanced and security profiles selected 86 completions, with critical-colony ending shortage of 190 and critical unserved demand of four. The service profile selected 87 completions, with those two deficits at 214 and seven. The comparison is within each weight profile. Score values across profiles use different definitions. On the surprise production-cut scenario, Phase 3 matched Phase 2 on the reported quality metrics.

All 36 Release x64 runs completed across nine four-mode groups. Seed 42 also reproduced Phase 2's small and medium standard outcomes under the balanced Phase 3 profile. Standard control outcomes matched the historical Phase 2 evaluation. A repeated small Phase 3 run matched the exact decision trace and objective telemetry. The matrix has one large seed and one timing observation per configuration.

## Feasibility-aware allocation study

Run the Release x64 study from the repository root using the preserved pre-decision-making executable and the current executable:

```powershell
./ChronosNexus/Run-Allocation-Study.ps1 -OriginalExecutable PATH_TO_ORIGINAL_EXE -Executable PATH_TO_CURRENT_EXE -OutputDirectory PATH_TO_NEW_RESULTS_DIRECTORY
```

The runner uses the same dataset generator, events, balanced objective, 35-second wall budget, and 1,024 MiB working-set budget for each configuration. See the [paired measurements](feasibility-aware-allocation.csv) and the [scarcity trace summary](feasibility-aware-scarcity-traces.csv). `tentative_cargo` sums requested upper bounds across probes and can count the same stock in several alternative attempts. `released_attempts` counts rejected probes whose trial state is discarded. No permanent reservation needs to be released for those failures. `trial_committed_cargo` precedes whole-plan objective acceptance, while `scheduled_aid_cargo` observes the retained schedule. Uninstrumented historical measurements are marked `NA`. The graph counter includes coordination expansions and the selected schedule's counters. It does not aggregate every discarded Phase 3 candidate. `completion_rate` uses scheduled plus rejected mission IDs, so its denominator changes with the set of aid missions; the fixed-obligation service measure remains available as `input_service`.

## Feasibility-aware allocation performance

```powershell
./ChronosNexus/Run-Allocation-Performance.ps1 -Executable PATH_TO_RELEASE_X64_EXE -OutputDirectory PATH_TO_NEW_RESULTS_DIRECTORY
./ChronosNexus/Run-Allocation-Performance.ps1 -Executable PATH_TO_RELEASE_X64_EXE -Diagnostic -OutputDirectory PATH_TO_NEW_DIAGNOSTIC_DIRECTORY
```

The normal study uses a 35-second wall limit and 1,024 MiB working-set limit, repeating both scarcity fixtures three times. The diagnostic option uses a 120-second wall limit. These limits are experimental. The runner compares the consolidated default, the previous full-ship aid search, and the indexed search in one binary. The SHA-256 field covers assignment, rejection, recovered mission, aid delivery, and civilization-score trace lines. The timing fields distinguish civilization planning, aid coordination, and its nominal replay. Reported route time is included in recovery time. The itinerary counter counts per-probe legacy validations and indexed initial or changed-tail validations. The setup timer includes collection of legacy plans or indexed lookup and maintenance. It excludes legacy booking replay, which is included in recovery time.

All ten completed legacy/indexed pairs matched on decision traces and reported quality. Both aid variants timed out during aid coordination on both Large cases at 35 seconds. The consolidated default completed both. A separate 120-second diagnostic of Large standard seed `20261120` timed out for both the preserved pre-index executable and the indexed executable. The pre-index executable predates the separate aid-stage marker. The indexed executable began aid coordination at 29.53 seconds. [Paired measurements](allocation-performance.csv) · [Diagnostic measurements](allocation-performance-diagnostic.csv)
