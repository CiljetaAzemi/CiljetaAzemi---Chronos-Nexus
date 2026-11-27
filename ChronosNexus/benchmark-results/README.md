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

### Reverse route bounds

```powershell
./ChronosNexus/Run-Allocation-Performance.ps1 -Executable PATH_TO_RELEASE_X64_EXE -RouteStudy -OutputDirectory PATH_TO_NEW_RESULTS_DIRECTORY
./ChronosNexus/Run-Allocation-Performance.ps1 -Executable PATH_TO_RELEASE_X64_EXE -RouteStudy -Diagnostic -OutputDirectory PATH_TO_NEW_DIAGNOSTIC_DIRECTORY
```

This study compares the consolidated default, the indexed allocator, and the indexed allocator with reverse bounds. It repeats both scarcity fixtures three times and uses production-cut seed `20261126` as the unseen case. The runner preserves the last flushed `AID_ROUTING` sample even if the process times out. Its counters are cumulative for completed probes. Edge counts include inspected adjacency entries, including unusable routes. An equivalent query has the same donor, receiver account, exact cargo, and route strategy. The study counts these repeats without adding a weighted route cache. The 120-second budget is a diagnostic limit.

All ten completed aid pairs matched on decision hashes and reported quality. Both aid variants timed out on both Large cases at 35 seconds. In the 120-second Large standard diagnostic, the indexed control timed out and the reverse-bound variant completed in 40.94 seconds. The last indexed sample counted 20,013 weighted searches and over one billion inspected edges, with zero equivalent repeats. The reverse-bound run evaluated the entire aid pass using 2,319 weighted searches and 497 reverse trees. These counts cover different amounts of completed work because the control timed out. [Paired measurements](aid-route-performance.csv) · [Diagnostic measurements](aid-route-diagnostic.csv) · [Large telemetry samples](aid-route-large-telemetry.csv)

## Expanded objective study

```powershell
./ChronosNexus/Run-Objective-Study.ps1 -Executable PATH_TO_RELEASE_X64_EXE -OutputDirectory PATH_TO_NEW_RESULTS_DIRECTORY
```

The study compares legacy Phase 3, expanded Phase 3, and expanded Phase 3 with indexed aid and reverse bounds. Limits are 45 seconds wall time and 1,024 MiB working set. The benchmark runner records `legacy_rescore` and `expanded_rescore` for the same realized plan. Compare each column only against the same formula and profile. Storage and runtime retain their usual benchmark definitions. The generated scenarios declare population and consumption but no development appraisals. The exact construction fixture in `--self-test` exercises the appraisal components separately. [Objective model and scope](../OBJECTIVE.md) · [Study measurements](objective-study.csv)

All 18 balanced Small and Medium runs completed with zero execution failures. The balanced expanded planner improved the expanded re-score in all six pairs (+0.57 to +1.49 points) and reduced the legacy re-score in all six. Ending shortage increased in every pair, and timely external fulfillment changed in only one pair, Small scarcity (+0.18 percentage points). This is a preference trade-off under the tested candidate set.

| Medium case | Ending shortage, legacy / expanded / expanded + aid | Fuel, legacy / expanded / expanded + aid | Completions, legacy / expanded / expanded + aid |
|---|---:|---:|---:|
| Standard | 1,909.70 / 2,090.00 / 2,276.32 | 10,550.90 / 10,550.90 / 8,771.15 | 238 / 238 / 193 |
| Scarcity | 5,404.63 / 5,456.93 / 5,600.93 | 3,158.91 / 3,158.91 / 1,966.92 | 68 / 68 / 50 |
| Production cut | 2,087.50 / 2,224.00 / 2,329.00 | 11,206.30 / 11,206.30 / 9,454.67 | 245 / 245 / 201 |

On Medium scarcity the security and service profiles matched the balanced profile on the reported service, shortage, completion, fuel, cost, waste, and risk outcomes. The exact controlled fixture detected preference-sensitive choices. Each benchmark cell is a single observation, so the study does not estimate timing variance. All three Large runs reached the 45-second limit during scheduling. Complete-plan quality is unavailable for Large. The CSV preserves each timeout and its last recorded stage.

## Computational budget study

```powershell
./ChronosNexus/Run-Anytime-Study.ps1 -Executable PATH_TO_RELEASE_X64_EXE -OutputDirectory PATH_TO_NEW_RESULTS_DIRECTORY
./ChronosNexus/Plot-AnytimeStudy.ps1 -SummaryCsv PATH_TO_ANYTIME_CSV -IncumbentsCsv PATH_TO_INCUMBENTS_CSV -OutputSvg PATH_TO_SVG
```

The controls use the same executable, datasets, seeds, constraints, and objective formulas, with the budget flags omitted. Seed `20261120` provides Large continuity. The other seeds exercise new standard, scarcity, production-cut, and urgent-event cases. The dynamic case adds a deterministic urgent request to the generated disruption stream. Study dimensions are 100/500/50, 1,000/10,000/500, and 10,000/100,000/5,000 planets, routes, and external missions before generated aid or activated urgent requests. Selected Small, Medium and Large budget configurations are repeated. The runner compares both objectives and includes experiments with indexed feasibility-aware aid and reverse bounds on selected Small and Medium cases.

An internal budget of zero in the CSV represents the unbudgeted control. The external watchdog remains active for all runs. Controls report simulated outcomes only when they complete. Budgeted runs have separate nominal incumbent samples and completed event simulation outcomes. The summary columns for realized scores, completions, shortage, service, and costs are unavailable when the event simulation does not finish. The first nonempty timestamp includes validation of the first nonempty candidate even if the active score favors an earlier incumbent. The logged unassigned count covers original external missions, before any urgent requests. Engine time is measured inside the executable. Wall time includes process launch, report I/O and watchdog sampling. Process peak working set is neither a heap allocation measurement nor a hard memory limit.

[Study summary](anytime-study.csv) · [Incumbent samples](anytime-incumbents.csv) · [Quality-versus-time curves](anytime-curves.svg) · [Budget implementation and analysis](../ANYTIME.md)

All 48 processes returned before the external watchdog. Of the 33 budgeted runs, 32 returned a validated incumbent and completed the event simulation. Twenty-two completed the full static candidate pipeline. The 1 ms run returned no validated plan. The maximum engine overshoot was 2.45 ms on that run. Engine time stayed within the requested limit for every tested budget of 25 ms or more. The maximum end-to-end wall overshoot was 102.23 ms, including launch and reporting. The largest budgeted engine working-set peak was 84.95 MiB. All 306 incumbent samples maintained strictly increasing scores under each run's active objective.

| Scenario and active objective | Budget | First nonempty candidate | Engine return | Completed / failed | Realized legacy / expanded |
|---|---:|---:|---:|---:|---:|
| Small standard, legacy | 25 ms | 6.05 ms | 21.72 ms | 19 / 1 | 55.335 / 73.591 |
| Medium standard, legacy | 100 ms | 16.16 ms | 90.08 ms | 79 / 0 | 52.761 / 74.759 |
| Medium standard, legacy | 750 ms | 14.64 ms | 414.52 ms | 251 / 1 | 56.880 / 74.344 |
| Large standard, legacy | 35 s | 0.145 s | 32.13 s | 2,414 / 13 | 56.652 / 74.071 |
| Large standard, legacy, two repeats | 45 s | 0.128–0.148 s | 42.11–42.14 s | 2,414 / 13 in both | 56.652 / 74.071 in both |
| Large dynamic, legacy | 35 s | 0.148 s | 32.10 s | 2,475 / 10 | 56.524 / 74.149 |

The current unbudgeted Large controls completed in 34.19 s and 35.96 s. These measurements differ from the earlier objective-study timeouts. Every budgeted Large run stopped during candidate planning and completed simulation with the same realized scores and mission counts as its paired control. Large standard published its final incumbent at 22.21–23.08 s. Recorded validation accounted for 0.66–0.79 s across budgeted Large runs. The original-obligation gaps remained substantial, with 3,017 external missions unassigned on Large standard and 2,971 on Large dynamic.

A generous budget reproduced the tested legacy controls' reported outcomes. The expanded model sometimes selected a materially different plan. On Medium dynamic it increased its realized score from 75.904 to 76.074 while completions dropped from 255 to 208 and ending shortage rose from 2,160.68 to 2,313. On Small scarcity the expanded score preferred the empty plan to serving six missions. This reflects the active objective's trade-off between reported stock security, costs, and service. The CSV retains both scores and raw outcomes. Rare wall-time or memory peaks outside these scenarios remain possible.

## Security-priority objective comparison

```powershell
./ChronosNexus/Run-Anytime-Study.ps1 -Refinement -Executable PATH_TO_RELEASE_X64_EXE -OutputDirectory PATH_TO_NEW_RESULTS_DIRECTORY
```

The refinement matrix compares both weighted objectives and the lexicographic security-priority policy on matched inputs. Where wall budgets are specified, the incumbent log contains the nominal decision coordinates and the summary reports outcomes only for completed event simulation. Medium scarcity includes indexed aid with reverse bounds. The security and service weight profiles are sensitivity cases. Compare those profiles on raw outcomes because their numerical scores use different weights. Recorded operating cost, fuel, waste and risk remain separate from measured computation.

At the 2.5 s Small and 5 s Medium budgets, security priority improved realized essential coverage and total unserved consumption over expanded weighted planning on all ten matched cases. It lowered total ending shortage on nine. The exception was Small scarcity seed `20261130`, where it served six missions and lowered critical shortage from 35 to 15, while total ending shortage rose from 491 to 527.7. Against the matched legacy controls, coverage improved in six cases and tied in four. Total ending shortage rose in two cases. The unseen Small dynamic case completed 20 missions versus 18 for legacy, with higher fuel use and 33 more units of ending shortage.

| Matched case | Policy | Completed / failed | Critical ending / critical unserved | Total ending / total unserved | Fuel |
|---|---|---:|---:|---:|---:|
| Medium scarcity, indexed aid, unbudgeted | Expanded | 50 / 0 | 350 / 30 | 5,600.93 / 1,664.07 | 1,966.92 |
| Medium scarcity, indexed aid, unbudgeted | Security priority | 67 / 0 | 0 / 0 | 5,177.53 / 1,435.07 | 3,579.82 |
| Medium urgent events, 5 s | Expanded | 208 / 2 | 200 / 0 | 2,313 / 1,213 | 9,034.21 |
| Medium urgent events, 5 s | Security priority | 256 / 2 | 150 / 0 | 1,879.86 / 145.72 | 10,974.48 |

In Medium scarcity, the security weighted profile selected the empty plan, while the service weighted profile completed 50 missions. Both left 350 units of critical ending shortage. On Medium urgent events those profiles completed 64 and 208 missions, respectively, and both left 200 critical units. This sensitivity supports reporting the raw service and security measures. The 100 ms runs on that case returned validated partial plans and no completed event simulation. For the first 5 s security-priority repeat, the nominal trace first validated a nonempty schedule by 44 ms, reached 64 assignments by 145 ms, and reached 257 by 566 ms. Essential coverage rose from 98.97% to 99.85%, and critical ending shortage fell from 200 to 150 along that trace.

[Small and Medium outcomes](security-priority-small-medium.csv) · [Small and Medium incumbent traces](security-priority-small-medium-incumbents.csv)

All ten Large processes returned a validated plan and completed the event simulation. Each hit its internal planning cutoff during scheduling with aid. The tested policies returned the same realized outcomes per input at 35 s and 45 s. Standard completed 1,953 missions with nine failures, while the unseen urgent-event case completed 1,956 with 15 failures. Both left 2,000 units of critical ending shortage. No construction was selected before the cutoff. Security-priority runs first validated a nonempty candidate in 0.273–0.397 s, returned in 32.23–42.47 s and peaked at about 85 MiB of working set. This measures the current candidate-generation bottleneck under a shared budget. Later alternatives were unavailable for comparison.

All 84 processes returned before the external watchdog. The three 100 ms Medium runs are the only runs without complete event simulation. Elapsed engine time stayed within the tested limits. The external wall time includes process launch and reporting and can exceed very short internal budgets.

[Large outcomes](security-priority-large.csv) · [Large incumbent traces](security-priority-large-incumbents.csv)

## Balanced multi-objective study

```powershell
./ChronosNexus/Compare-BalancedCandidates.ps1 -Executable PATH_TO_RELEASE_X64_EXE -OutputCsv PATH_TO_CANDIDATE_CSV
./ChronosNexus/Run-Anytime-Study.ps1 -BalancedStudy -Sizes small,medium,large,extreme -Executable PATH_TO_RELEASE_X64_EXE -OutputDirectory PATH_TO_NEW_RESULTS_DIRECTORY
./ChronosNexus/Plot-AnytimeStudy.ps1 -BalancedStudy -SummaryCsv PATH_TO_ANYTIME_CSV -IncumbentsCsv PATH_TO_INCUMBENTS_CSV -OutputSvg PATH_TO_SVG
```

The common-pool audit exports validated nominal Phase 3 candidates before any indexed aid pass. Its Pareto labels include coverage, service, critical and total shortages and unmet consumption, transportation, fuel, waste, risk, and construction. The end-to-end study also exercises indexed aid on Medium scarcity. The newer standard and production-cut seeds are held out from tolerance selection. Extreme is a preliminary standard synthetic case with an external watchdog, a sampled 1,024 MiB threshold, and 60, 120 and 300 second internal budgets. The initialization timestamp includes generation, objective setup, and preliminary reporting. Search counters describe the retained schedule only. A global search-space reduction percentage requires a comparable unpruned search and is unavailable.

### Common validated candidate pool

Medium scarcity yielded six candidates, five nondominated on the recorded coordinates. The banded selector chose candidate 2 and strict priority and the reference constraint chose candidate 5. Both had 170 units of critical ending shortage. Candidate 2 had higher service and no construction expenditure, while candidate 5 had higher coverage, lower fuel, and lower total unmet consumption. Indexed aid was excluded from this common pool. Medium urgent events yielded six nondominated candidates. Strict, banded, constrained and legacy selection chose candidate 3. The lowest-fuel nonempty candidate used 3.02% less fuel, with lower coverage and timely service. No cheaper candidate met the exact reference constraints. The observed alternatives do not establish that a cheaper joint allocation or route is impossible. [Candidate pool and Pareto labels](balanced-candidates.csv)

### End-to-end Medium trade-offs

| Case and policy | Completed | Critical ending / unmet | Total unmet | Timely service | Fuel |
|---|---:|---:|---:|---:|---:|
| Scarcity, expanded indexed, unbudgeted | 50 | 350 / 30 | 1,664.07 | 6.05% | 1,966.92 |
| Scarcity, strict indexed, unbudgeted | 67 | 0 / 0 | 1,435.07 | 5.81% | 3,579.82 |
| Scarcity, banded indexed, unbudgeted | 67 | 0 / 0 | 1,620.07 | 6.05% | 3,523.64 |
| Urgent events, expanded, 5 s | 208 | 200 / 0 | 1,213.00 | 34.94% | 9,034.21 |
| Urgent events, strict or banded, 5 s | 256 | 150 / 0 | 145.72 | 36.10% | 10,974.48 |

All three indexed band widths returned the same scarcity outcome at 5 s. Against strict indexed aid, the balanced policy saved 1.57% fuel and 3.43% transport, selected zero rather than five projects, preserved both critical outcomes and all 17 additional completions, and retained about 19% of the strict reduction in total unmet consumption against the unbudgeted expanded reference. The coverage improvement retained was about 11%. No cheaper equivalent urgent-event plan was found. Strict and balanced urgent-event repeats had identical reported outcomes; engine times were 0.414–0.451 s and 0.428–0.429 s respectively. These repeats do not isolate comparator overhead.

The held-out Small standard case tied strict priority on coverage, completions, unmet demand, and fuel. On held-out Medium production cut, the balanced and strict policies each completed 248 missions and left 150 critical units short. Balanced planning left 341 units of total unmet consumption versus 293.5 under strict priority, with slightly higher fuel use. These regressions place limits on adopting the banded policy as a default.

On the held-out Large standard case, all six runs completed event simulation. All three policies stopped during candidate planning at 35 s and finished static planning at 45 s. Both strict and balanced priority completed 2,495 missions with 14 failures and 1,505 units of critical ending shortage. Balanced had no fuel savings and left 2,276.26 units of total unmet consumption versus 1,913.31 under strict priority. The legacy plan completed 2,492 with 15 failures and left 2,034.31 units of total unmet consumption. The 45 s strict and balanced runs took 36.78 and 36.87 s and peaked at 84.81 and 85.21 MiB respectively.

[Small, Medium and Large results](balanced-study.csv) · [Incumbent traces](balanced-incumbents.csv) · [Coverage-versus-time curves](balanced-curves.svg)

### Preliminary Extreme study

The retained observations use one seed and the same 100,000 planets, 1,000,000 routes, 50,000 original missions, and event stream. The 60 and 120 s legacy runs executed sequentially. The strict and balanced observations executed as a concurrent cohort, overlapping part of the 300 s legacy run. The CSV records this execution context. Use `-Sizes extreme -ExtremePolicy POLICY -ExtremeBudgetsMs LIMIT` to reproduce an individual cell. A duplicate strict launch from the sequential batch was stopped once the three legacy rows had been saved. The independent strict cohort supplied the reported rows. Differences in time and assignment counts across execution contexts cannot isolate policy overhead or establish a quality ranking under equal resources.

| Policy | Assigned at 60 s | Assigned at 120 s | Assigned at 300 s | Completed / failed at 300 s |
|---|---:|---:|---:|---:|
| Legacy | 73 (0.146%) | 206 (0.412%) | 462 (0.924%) | 460 / 2 |
| Strict | 8 (0.016%) | 76 (0.152%) | 434 (0.868%) | 432 / 2 |
| Balanced | 27 (0.054%) | 77 (0.154%) | 434 (0.868%) | 432 / 2 |

All nine retained runs stopped during initial scheduling and completed event simulation of their validated partial plans. Generation took 0.49–1.18 s, initialization finished by 1.27 s, and the first validated nonempty candidate arrived in 2.40–5.20 s. Peak working sets were approximately 743–745 MiB. All retained runs met their time budgets and stayed below the sampled 1,024 MiB threshold. All left 20,000 units of critical ending shortage. Realized essential coverage was 98.876–98.879%, which reflects substantial existing supply alongside minimal mission assignment. At 300 s the balanced and strict plans each consumed 25,843.71 fuel and left 201,535 units of total ending shortage. The retained schedules recorded roughly 139 million graph-node expansions each. Later improvement stages were unreached, so their pruning counters were zero. No comparable unpruned Extreme control was run.

[Extreme outcomes and execution context](balanced-extreme.csv) · [Extreme incumbent traces](balanced-extreme-incumbents.csv) · [Extreme assignments over time](balanced-extreme-curves.svg)

## Initial scheduling scalability

### Exact route-search reduction

Run the same study command below with `-RouteReductionStudy` in place of `-InitialSchedulingStudy` to add the indexed-plus-pruning variant. The urgent-event case has two repetitions per mode. The pruned variant uses the same objective, mission ordering, constrained routes and reservation checks. On the matched Large case it issued 5,016 searches versus 8,402 (40.3% fewer), expanded 6.25 million nodes versus 11.30 million (44.6% fewer) and reduced initial scheduling from 6.45 s to 4.56 s. Final assignment hashes matched. Compare expansion percentages only at equivalent decision work. Within fixed Extreme budgets, faster variants can expand more nodes while processing more missions. [Pruning argument and routing alternatives](../ANYTIME.md#exact-carrier-and-fuel-pruning)

The Large indexed control averaged 1,136 expanded nodes per found query and 9,795 per missing query. It captured 6,240 exact cache hits, while the Extreme 60 s indexed control captured only a few. The Large pruned pass made 2,232 carrier rejections and skipped 75 minimum-time fallbacks on fuel grounds. Cache hits describe the queries actually made and retained by the cache. They do not measure overlap across distinct cargo, weights or endpoints.

| Extreme budget | Hash attempts/s and assignments | Indexed attempts/s and assignments | Pruned attempts/s and assignments |
|---|---:|---:|---:|
| 60 s | 9.97 / 55 | 20.04 / 108 | 56.30 / 436 |
| 120 s | 9.49 / 108 | 21.76 / 334 | 55.65 / 1,154 |

All six runs returned a validated partial plan and completed event simulation. At both budgets, every indexed assignment line appeared verbatim in the corresponding pruned trace. At 120 s, pruning evaluated about 2.6 times as many attempts with 2.9% fewer aggregate graph expansions. The pruned plan completed 1,148 missions with six failures, up from 332 completions and two failures. Critical ending shortage was unchanged at 20,000. The primary limiting stage remained initial scheduling.

[Route-reduction measurements](route-reduction-study.csv) · [Validated nominal incumbents](route-reduction-incumbents.csv)

The 300 s follow-up ran only the promising pruned mode. Use `-RouteReductionStudy -Sizes extreme -InitialVariants pruned -ExtremeBudgetsMs 300000`. It validated 5,841 assignments (11.68%) at 297.93 s and returned in 299.87 s wall time, peaking at 755.51 MiB. Event simulation did not finish. The 18.14% timely fulfillment, 20,000 critical ending shortage, and 127,403.73 unmet consumption at that checkpoint are nominal forecasts. The earlier indexed run assigned 2,200 and completed event simulation. It is a historical reference, with no same-session 300 s timing control. [Follow-up summary](route-reduction-300.csv) · [Follow-up incumbents](route-reduction-300-incumbents.csv)

```powershell
./ChronosNexus/Run-Anytime-Study.ps1 -InitialSchedulingStudy -Sizes small,medium,large,extreme -ExtremeBudgetsMs 60000,120000 -Executable PATH_TO_RELEASE_X64_EXE -OutputDirectory PATH_TO_NEW_RESULTS_DIRECTORY
```

The runner compares the original and indexed routing workspaces sequentially with the same Legacy objective, mission order, seeds, constraints and generated event streams. The external watchdog and 1,024 MiB sampled working-set limit remain active. Assignment hashes include mission, spacecraft, departure, arrival and exact route data. Compare hashes only when decision work finishes or a fixed prefix is matched. Runs interrupted at the same wall budget can process different numbers of missions. Route nodes and edges count completed shortest-path queries, and route elapsed time includes interrupted queries. [Instrumentation and complexity](../ANYTIME.md)

All six completed Small, Medium and Large pairs produced identical final assignment hashes and realized outcomes. The indexed initial scheduler was 1.29–1.93 times as fast over these pairs. On Large, route searches accounted for 8.44 of 8.89 s of baseline initial scheduling. Inventory probes accounted for approximately 0.019 s. The indexed pass took 5.77 s and issued the same 8,402 searches, explored the same 11.30 million vertices and inspected the same 112.24 million adjacency entries. Both had 6,240 existing route-cache hits, 40,618 ship inspections and 44,755 tentative inventory probes. The retained indexed workspace capacity was 454,272 bytes. The whole engine took 37.95 s for the control and 35.28 s indexed, so most elapsed time remained in later steps.

| Extreme budget | Tested per initial-scheduler second, hash / indexed | External assignments, hash / indexed | Completed / failed, hash → indexed |
|---|---:|---:|---:|
| 60 s | 12.32 / 26.45 | 71 / 167 | 71 / 0 → 167 / 0 |
| 120 s | 14.49 / 29.55 | 200 / 502 | 199 / 1 → 499 / 3 |
| 300 s | 16.25 / 36.22 | 756 / 2,200 | 752 / 4 → 2,189 / 11 |

All four runs returned validated partial plans and completed event simulation. The common nominal prefix samples matched on both scores and every recorded quality metric. Every assigned trace line in each hash control also appeared verbatim in the indexed trace at the same budget. At 120 s, timely external fulfillment rose from 1.16% to 2.66% and unmet consumption fell from 134,218.42 to 134,005.42. Ending shortage rose from 200,831 to 201,637. Critical ending shortage stayed at 20,000. More missions also increased transport, fuel and risk. The 120 s indexed peak was 748.08 MiB versus 744.03 MiB for hash routing. The workspace reserved about 4.04 MiB. Equal-budget comparison therefore measures more search completed within the limit, alongside the consequences of assigning more missions.

[Sequential results and assignment hashes](initial-scheduling-study.csv) · [Validated incumbent samples](initial-scheduling-incumbents.csv)

The 300 s pair was run sequentially after the shorter comparisons showed higher throughput. To reproduce it, use the same command with `-Sizes extreme -ExtremeBudgetsMs 300000` and a fresh output directory. All 756 hash assignments appeared verbatim in the indexed trace. Both runs stopped during initial scheduling and completed simulation. Indexed assigned 4.40% of external missions versus 1.51% for hash, and peak working set rose from 746.75 to 752.56 MiB. Wall times were 298.41 and 298.91 s, respectively. Timely fulfillment rose from 3.80% to 9.11% and unmet consumption fell by 1,688.27 units. Critical ending shortage held at 20,000. Total ending shortage rose by 2,524.27 units, and fuel rose from 44,287.70 to 123,902.12. The larger plan also had higher transportation costs, waste and risk. The same-budget results show a capacity improvement with mixed civilization outcomes.

## Extended search preliminary evaluation

Run `./ChronosNexus/Compare-LongSearch.ps1 -Executable .\x64\json-bin\ChronosNexus.exe -OutputDirectory .\x64\long-study -Seconds 5` from the repository root. The twelve preliminary runs were sequential. All used the Legacy objective, pruned indexed construction and indexed aid with reverse bounds, with identical inputs within each case. All completed event simulation.

| Case | Baseline realized Legacy | Fixed | Adaptive |
|---|---:|---:|---:|
| Small standard | 58.2487 | 58.4838 | 58.5471 |
| Small scarcity | 54.2804 | 54.2929 | 54.2929 |
| Small urgent events | 52.2803 | 52.2803 | 52.2803 |
| Medium production cut | 61.0162 | 61.0162 | 61.0162 |

On Small standard, adaptive completed 26 missions versus 27 for the baseline and left 62.48 units of unmet consumption versus 52.48. The scalar improvement therefore came with a realized welfare trade-off. At the 60-second budget the adaptive standard run improved the nominal score from 58.7871 to 59.0712, with its final best found at 2.11 s. It evaluated 64,663 feasible long-search candidates, skipped 6,026 recent duplicate signatures, and completed simulation. Peak process working set was 9.27 MiB. It found no later improvement in that run, and no proof of exhaustion. This is evidence of early improvement and subsequent stagnation on one seed. It does not establish multi-hour benefit or adaptive superiority. Large and Extreme extended-search experiments remain pending. The prior routing studies characterize initialization only.

Checkpoint validation separately recovered an incumbent and increased the saved iteration count from 1,547 to 3,133, accepted a compatible replay, and rejected a different seed. Release x64 and the regression suite passed, including exhaustive comparison of the two feasible portfolios in the one-project fixture. Full-duration results remain pending.

[Paired summary](long-search-study.csv) · [Incumbent curves as CSV](long-search-progress.csv) · [One-minute trace](long-search-minute.csv) · [Multi-hour commands](../ANYTIME.md#running-and-recovery)

### Neighborhood redesign experiment

The development matrix compares the baseline, historical fixed and adaptive search, and two booking-neighborhood variants. Use `Compare-LongSearch.ps1 -Neighborhoods -Seconds 5` with the usual executable and output-directory parameters. Use `-HeldOut` for the separate Small seeds. `-Modes` and `-Scenarios` select subsets. The first version overused its leading operator and grew the edit radius linearly. The revised version switches to round robin during stagnation, grows the edit radius logarithmically and samples initially local carriers directly. Its checkpoint identity differs from the first version.

The revised 30 s standard comparison reached nominal Legacy 59.09389 with improving-only neighborhoods, 59.07124 with historical adaptive search and 58.97680 with downhill acceptance. The latter improved as late as 12.47 s, but did not catch the other modes. The 5 s revised scarcity and urgent-event cases improved by about 0.01451 and 0.00179 realized points over the historical adaptive control. The revised Medium production-cut candidates also improved over the historical control. A reported improvement after exploratory acceptance is a sequence observation, without proof that the downhill move was necessary.

[First design outcomes](neighborhood-v1.csv) · [Revised development outcomes](neighborhood-v2-5.csv) · [Convergence curves](neighborhood-convergence.svg) · [30-second ablation](neighborhood-v2-30.csv)

On held-out five-second cases, the revised exploratory mode improved realized Legacy relative to historical adaptive search by 0.05063 on standard, 0.71139 on scarcity, and 0.01005 on urgent events. Mission completions and unmet consumption matched historical adaptive on each of these three cases. The strongest scarcity gain reduced critical ending shortage from 4.00 to 1.75 and fuel from 371.44 to 328.79 while raising transportation cost from 312.32 to 330.23. The improving-only ablation matched the exploratory variant on scarcity and urgent events and was slightly behind it on held-out standard. The 60 s development urgent-event run found its best around 0.02 s despite 212,448 attempts, so its extra budget did not improve the result. The observed duplicate count covers the bounded recent cache and cannot establish global diversity or provable exhaustion.

All sampled revised traces checked so far were nondecreasing in nominal Legacy. A short interrupted-run checkpoint test resumed from iteration 2,526 to 4,757, with its saved nominal Legacy rising from 53.07452 to 53.09232. The previous tiny exact regression remains a one-project case. The redesigned operators have no exact gap for these generated galaxies.

[Held-out outcomes](neighborhood-heldout.csv) · [Held-out progress](neighborhood-heldout-progress.csv) · [60-second development outcomes](neighborhood-v2-60.csv)

The five-minute follow-up on held-out Small standard finished event simulation with 13 completions and zero failures. Its nominal best was 56.39910 at 6.45 s, and its realized Legacy score was 56.32274. It generated 1,406,887 attempts, skipped 239,660 recent duplicates and completed 1,167,227 nominal evaluations. Peak working set was 8.62 MiB. This run improved slightly over the five-second outcome but added no evidence of improvement after the first minute. Keep the default planner. The next research question is whether coordinated repairs with additional decision variables can improve the plateau, rather than increasing the budget for this neighborhood mix. Budgets from 15 minutes to eight hours remain untested.

[Five-minute outcomes](neighborhood-v2-300.csv) · [Five-minute progress](neighborhood-v2-300-progress.csv) · [Convergence curve](neighborhood-five-minute.svg)

### Exact hybrid comparison

The six configurations ran sequentially at five seconds on each held-out Small case, using the same Legacy objective and scenario. All 18 completed event simulation. The exact hybrid improved realized Legacy over the strongest historical variant on standard (56.67484 versus 56.30060) and urgent events (58.12866 versus 57.89919). It completed one and three fewer missions respectively, with 3.0 and 3.5 more units of unmet consumption. Scarcity exhausted the restricted menu after 2,880 replay attempts and returned 52.87579. The neighborhood search reached 53.56230 under the same nominal objective, leaving at least 0.68651 Legacy points outside the exhausted menu. The full-problem gap remains unknown.

At 30 seconds, standard reached nominal 56.78578 at 23.01 s and urgent events reached 58.71638 at 13.62 s. Neither exhausted its menu. Both scores improved over the five-second observations. Standard realized unmet consumption rose to 89.172. Longer enumeration is demonstrably able to improve this scalar on two cases, with no evidence here beyond 30 seconds or of broad quality superiority. A separate scarcity replay recovered the checkpoint with the same realized score. Release x64 and all self-tests passed.

[Matched results](exact-hybrid-5.csv) · [Five-second curves](exact-hybrid-curves.svg) · [30-second results](exact-hybrid-deeper.csv) · [30-second traces](exact-hybrid-deeper-progress.csv) · [Restricted model and recovery](../ANYTIME.md#exact-hybrid-for-small-instances)

### Expanding master and native cargo probe

The matched Small scarcity case used seed 20270119 and the same Legacy objective, event stream, and one-minute budget. Exhaustive pairing admitted 30,163 columns, finished 690, and reached 53.80834 with 1.006 units of critical ending shortage. Geometric pairing admitted 29,895, finished 17,045, and reached 54.14372 with zero critical ending shortage. That matched the same-budget improving-only control, with six completions and 125 units of unmet consumption. The geometric pass recorded eight master improvements versus three for exhaustive pairing. The five-minute exhaustive run also reached 54.14372, at 90.45 seconds. Geometric pairing reached it at 24.83 seconds. The comparison establishes better use of the tested time budget on this seed. It does not establish a global gap or an eight-hour benefit.

The standalone HiGHS 1.16.0 Windows x64 probe solved the four-leg continuous cargo LP in approximately 2.3 ms on the preserved run. The analytic optimum is 14.25 and the recipient-first cost is 37, a 61.49% reduction for the fixed-departure fixture. The release was retrieved through an approved network retry. From an x64 Developer PowerShell with the archive extracted, run:

```powershell
.\ChronosNexus\Run-Highs-Cargo-Probe.ps1 -HighsRoot .\x64\solver-probe\release -OutputDirectory .\x64\solver-probe\reproduction
```

[One-minute exhaustive comparison](expanding-scarcity-60.csv) · [Geometric comparison](expanding-v2-60.csv) · [Geometric master trace](expanding-v2-60-master.csv) · [Five-minute exhaustive results](expanding-scarcity-300.csv) · [Native cargo probe](highs-cargo-probe.csv)

The backpressure refinement retains the existing admission cap and geometric pairing, with an explicit oldest-column probe. At 60 s it admitted 24,537 columns and completed 24,504, leaving 33 pending versus 12,850 in the geometric-only run. It reached the same 54.14372 Legacy score at 17.40 s versus 24.83 s. Peak working set fell from 111.39 to 91.74 MiB. It generated 629,048 column observations, identified 604,511 canonical repeats, rejected 212,987 incompatible substitutions before replay, and obtained four master improvements. These counts measure distinct itinerary encodings and actual frontier work. They do not establish dominance or count globally distinct civilization plans.

The 30 s welfare-priority scarcity run completed six missions with zero failures, eliminated critical ending shortage, and recorded 89 units of unmet consumption versus 125 under the observed Legacy plan. Its Legacy score was 53.22966. These are illustrative outcomes under different policies and budgets, without an isolated causal estimate of the policy effect. Welfare prioritizes consumption and service ahead of weighted operational savings.

At five minutes, backpressure finished all 29,756 admitted columns and performed 180,668 master replays, including four incumbent improvements. It finished at 297.35 s, completed event simulation, and peaked at 112.55 MiB. Final realized Legacy stayed 54.14372, with zero critical ending shortage, six completions and 125 unmet units. Its last improvement occurred at 15.65 s. After the key-byte cap filled, 1,264,604 nonduplicate column observations could not enter the master. Complete generator proposals could still compete, but their combination space ceased expanding. There is no evidence here of productive multi-hour use or an optimality certificate. The 15-minute, 30-minute and eight-hour configurations remain unmeasured.

A resumed one-minute checkpoint advanced from column 24,504, partner 7, to column 28,467, partner 0. Cumulative master replays advanced from 146,317 to 179,569 and the Legacy incumbent remained 54.14372. Recovery kept the frontier and did not restart enumeration at column zero. Release x64, all self-tests, and the native probe reproduction passed.

[Backpressure at one minute](expanding-v3-60.csv) · [Welfare-priority scarcity](expanding-v3-welfare-30.csv) · [Backpressure at five minutes](expanding-v3-300.csv) · [Master counters](expanding-v3-300-master.csv) · [Convergence curve](expanding-v3-curves.svg)

## Submission verification

The delivery configuration uses pruned indexed initial scheduling and welfare-priority comparison. Small extended experiments add the existing expanding hybrid and indexed aid with reverse bounds. The Medium, Large, and Extreme delivery cells use the fixed anytime pipeline. The historical default remains available. Both sides of each pair used seed 20261217, the same generated event stream and the same limits. The baseline used Legacy and the original initial scheduling path. This is an end-to-end configuration comparison, so it does not isolate the effect of one change. Legacy and Expanded were separately recomputed for both outputs. The listed times are engine times.

| Size | Budget | Baseline completed / failed | Delivery completed / failed | Baseline / delivery engine s | Delivery peak MiB | Static planning / event replay |
|---|---:|---:|---:|---:|---:|---|
| Small | 5 s | 27 / 1 | 29 / 1 | 0.04 / 1.00 | 6.97 | Budget stop / complete |
| Medium | 8 s | 248 / 2 | 248 / 2 | 1.00 / 0.77 | 14.28 | Complete / complete |
| Large | 45 s | 2,404 / 11 | 2,404 / 11 | 40.74 / 37.00 | 85.62 | Complete / complete |
| Extreme | 60 s | 65 / 1 | 467 / 3 | 57.30 / 57.19 | 746.56 | Partial at initial scheduling / complete |

All eight returned validated nominal plans and completed the event simulation. The Small delivery case left zero critical ending shortage versus 15 in the baseline, and reduced unmet consumption from 33.78 to 11.83. Overall ending shortage increased from 122.34 to 132.59. Medium unmet consumption dropped from 254.96 to 248.79 with slightly higher ending shortages. Large selected 64 construction projects in both modes. The Extreme delivery plan left 49,531 of 50,000 original missions unassigned, selected no construction before the cutoff, and applied 1,001 events. The final simulation is evidence that a budgeted partial plan can execute at Extreme scale. It does not establish high assignment coverage or full planning completion. The repeated event-simulation route counts describe applied replanning operations. Search-space reduction is reported on matched work in the earlier route-search study; a global percentage for runs with different search effort has no measured denominator.

Reproduce from the repository root with PowerShell 7 after a Release x64 build:

```powershell
.\ChronosNexus\Test-Delivery.ps1 -Executable .\x64\hybrid-bin\ChronosNexus.exe -OutputDirectory .\x64\delivery-reproduction
```

[Eight-run measurements](delivery-end-to-end.csv) · [Matched pruning study](route-reduction-study.csv) · [Build and execution](../BUILD.md)

The submission runner was exercised for 60 s followed by an 8 s resume using the same executable, seed, strategy, and welfare policy. It produced readable JSON results and checkpoint files, CSV progress and milestones, and an SVG chart. The first segment returned after 57.10 engine seconds with 30 completed missions, one failure, zero critical ending shortage, and 12.12 unmet units. The final saved master cursor advanced from 9,352 to 9,913 on resume and cumulative master replays advanced from 88,004 to 94,344. The same incumbent was retained. Its Legacy and Expanded realized cross-scores were 57.73868 and 73.39116. The eight-hour run itself has not been performed.

The JSON examples were also exercised independently. The shortage scenario completed one delivery and exported one preventive decision. The replanning scenario applied four events and exported four repair decisions, with two completions and one failure.

[Runner progress](delivery-minute-progress.csv) · [Milestones](delivery-minute-milestones.csv) · [Final JSON](delivery-minute-final.json) · [Convergence chart](delivery-minute-convergence.svg)

### Final pre-experiment comparison

The bounded preflight compared four configurations on held-out Small scarcity and urgent-event cases (seeds 20270119 and 20270120), with the same 12-second budget, welfare policy, pruned initial scheduler, indexed aid and reverse bounds. All eight runs returned validated plans and completed simulation.

| Scenario | Mode | Completed / failed | Critical ending shortage | Unmet consumption | Realized Legacy / Expanded |
|---|---|---:|---:|---:|---:|
| Scarcity | Fixed anytime | 6 / 0 | 4 | 89 | 51.93680 / 65.99494 |
| Scarcity | Improving-only neighborhood | 6 / 0 | 1.75 | 89 | 52.64824 / 66.04702 |
| Scarcity | Static exact hybrid | 6 / 0 | 4 | 89 | 51.96142 / 66.04445 |
| Scarcity | Expanding hybrid | 6 / 0 | 1.002 | 89 | 52.89555 / 66.04787 |
| Urgent events | Fixed anytime | 22 / 2 | 0 | 14.41 | 57.35490 / 72.81990 |
| Urgent events | Improving-only neighborhood | 21 / 2 | 0 | 29.04 | 57.42855 / 72.99104 |
| Urgent events | Static exact hybrid | 19 / 2 | 0 | 29.04 | 57.56677 / 73.20321 |
| Urgent events | Expanding hybrid | 21 / 2 | 0 | 29.04 | 57.45526 / 73.03657 |

Keep expanding hybrid with welfare for the requested Small long-run experiment. It found a better scarcity plan at the same fuel as the other two search modes and had five and seven master improvements across these two inputs. Fixed anytime remains the conservative operational choice when realized urgent-event welfare matters. Static exact exhausted the scarcity menu in under a second without matching expanding search. Improving-only search skipped 80,888 recent repeats in 83,666 scarcity attempts, compared with 20,179 in 43,834 generator attempts for expanding search. The latter also evaluated master substitutions, so the two denominators describe different work. Expanding finished 4,004 of 4,011 admitted scarcity columns, but used about 22.54 MiB versus 6.84 MiB for improving-only. The historical five-minute Legacy case filled the pool and stopped improving at 15.65 seconds. Short-run evidence does not establish useful eight-hour convergence.

Use the delivery matrix for Medium, Large and Extreme recommendations. The 60 s Extreme plan is a partial schedule and should be presented with its unassigned count. Search-space reduction remains a matched-work statistic. Overall evaluation coefficients are experimental. The broad dynamic core handles changes to all four required entity classes. The three unsupported event examples listed in the root requirements check remain limitations, as do survival measurement and process-wide search-state accounting.

[Matched outcomes](final-preflight-comparison.csv) · [Progress](final-preflight-progress.csv) · [Convergence](final-preflight-convergence.svg)
