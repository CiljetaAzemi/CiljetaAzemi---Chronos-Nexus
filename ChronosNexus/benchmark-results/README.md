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

The [runner](../Run-Benchmarks.ps1) writes CSV rows, progress markers, and per-run standard output and error files. The repository evidence consists of the three CSVs linked above. Log filenames in their columns identify outputs generated by the runner.

## Limitations

These are single-seed, single-run observations. Timing differences, especially at subsecond durations, do not establish repeatable speedups. Ordinary synthetic disruptions had limited impact on selected routes. The stress case intentionally targets selected edges and does not measure fleet-wide or inventory replanning. The workload does not establish performance on unseen competition scenarios, and the reported score uses the experimental weights defined in the technical report.
