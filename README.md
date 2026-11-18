# Chronos Nexus

Chronos Nexus is a C++20 console simulation for the university algorithms project. It combines an adjacency-list graph, Dijkstra routing, greedy resource allocation and mission scheduling, an event queue, and budgeted infrastructure selection using 0/1 Knapsack.

## Build and run in Visual Studio

1. Open `ChronosNexus/ChronosNexus.slnx` in Visual Studio with the Desktop development with C++ workload, MSVC v145, and a Windows 10 SDK.
2. Select **Release** and **x64**, then choose **Build → Build Solution**.
3. Open **Project Properties → Configuration Properties → Debugging** and set **Command Arguments** to one of the following. Start without debugging with **Ctrl+F5**.

| Run | Command arguments |
|---|---|
| Regression tests | `--self-test` |
| Original demonstration | Leave blank |
| Small, 100 planets, 500 routes, 50 input missions | `--benchmark small 20261118 custom` |
| Medium, 1,000 planets, 10,000 routes, 500 input missions | `--benchmark medium 20261118 custom` |
| Large, 10,000 planets, 100,000 routes, 5,000 input missions | `--benchmark large 20261118 custom` |
| Extreme, 100,000 planets, 1,000,000 routes, 50,000 input missions | `--benchmark extreme 20261118 custom` |

To enforce runtime limits and save CSV output, run the following from the repository root in a Visual Studio Developer PowerShell. The runner applies the limit by polling the process and terminates the benchmark process on a timeout or working-set limit. A direct IDE run does not apply those limits.

```powershell
.\ChronosNexus\Run-Benchmarks.ps1 -TimeoutSeconds 600 -MemoryMiB 1024
.\ChronosNexus\Run-Benchmarks.ps1 -Sizes small,medium -Seed 20261118 -Modes earliest,basic,all-routes,greedy-build
.\ChronosNexus\Run-Benchmarks.ps1 -Sizes medium -Modes stress,stress-all-routes -OutputDirectory .\ChronosNexus\benchmark-results\stress
```

The default executable path is `ChronosNexus/x64/Release/ChronosNexus.exe`. Use `-Executable` for an alternate output path. If the local linker reports LNK1101 about MSPDB140.DLL, the following measurement build skips PDB and manifest generation in that shell while retaining Release code optimization. The Visual Studio project still uses its usual settings by default.

If PowerShell blocks local scripts, open a new PowerShell with `powershell.exe -NoProfile -ExecutionPolicy Bypass` for the benchmark commands. This only applies to that process.

```powershell
$env:_LINK_ = '/DEBUG:NONE /INCREMENTAL:NO'
msbuild .\ChronosNexus\ChronosNexus.slnx /t:Build /p:Configuration=Release /p:Platform=x64 /p:GenerateManifest=false /p:TrackFileAccess=false /m:1 /nr:false
Remove-Item Env:_LINK_
```

## Experimental model

The generator uses `std::mt19937` and the selected 32-bit seed. The input mission count is exact. Aid proposals are generated in addition to those missions. Directed routes include a ring, mostly regional connections, and long-range wormholes. Colonies and stations have separate location IDs and route links. Food and medicine have independent stocks, rates, and storage limits. The participating aid network consists of designated surplus and deficit planets and colonies. Many other planets still hold inventory and participate in input missions. The simulation tracks stock through day 30 for every comparison. Disruptions are fixed by seed and include route closures, ship losses, and production decreases.

Distances are synthetic distance units. Route travel time is in days at reference speed 1 and is divided by each ship's positive speed multiplier. Fuel uses independent edge costs. Capacities are per-ship and per-edge cargo limits, and operating cost is credits per distance unit. Risk is an additive exposure index. Reliability is used in route ranking as an additive failure penalty and in ship eligibility. The experiment does not sample an independent travel-time or route-failure distribution during each trip.

| Mode | Changed algorithm |
|---|---|
| `custom` | Weighted ship heuristic, route-aware allocation, revalidation of recorded routes, and Knapsack |
| `earliest` | Earliest feasible arrival among the same candidates. Mission order and route feasibility checks are shared. |
| `basic` | Greedy transfer proposals ignore route costs and reachability at allocation. All proposals face the same scheduler and simulator checks. |
| `all-routes` | Recompute every eligible departure route, including unaffected routes. This tests route recomputation only. Fleet assignments and inventory plans are kept fixed. |
| `greedy-build` | Select facilities by value per credit under the same budget. |
| `stress` / `stress-all-routes` | Compare conditional and unconditional route recomputation against the same extra closures. The stress scenario closes selected exact edges immediately before scheduled departures. |

The score used for this baseline is `10*completed - 10*failed - 5*rejected + delivered - ending_shortage - stockout_amount - waste - 0.01*transportation_cost - risk + construction_value`. The mission terms penalize both execution failures and rejection. Delivery and stock terms value resource security. Infrastructure contributes its modeled strategic value. These are classroom experiment weights, with no claim that the professor will use them. Shortage is the final gap below target stock. Stockout amount is the accumulated unserved demand due to negative net production. Waste includes overflow and lost in-transit cargo. Rejected input and aid missions are counted separately from failed scheduled missions.

The runner records Windows peak working set when available, wall time including startup and polling, engine time, graph nodes expanded, allocation route queries, ship-route checks, and replanning time. Finished runs can report their own peak working set. A killed process can only provide the highest peak reported during polling. Population survival, general state-space exploration, and percentage reduction are recorded as `NA`. Failed budget runs retain the last progress marker and available peak memory. Full logs and `results.csv` are written under `ChronosNexus/benchmark-results` by default.

## Current limitations

- Route selection tries weighted, minimum-fuel, and minimum-time paths. This can miss feasible routes between those extremes. It does not reposition or refuel ships or constrain aggregate same-edge traffic.
- The simulator reserves a ship until its actual arrival. A route closure only affects pending departures. Ship loss causes failure but does not trigger reassignment to another ship. A production shock changes inventory dynamics without creating new missions during execution.
- The aid deadline is based on initial depletion time. It does not account for all confirmed arrivals or jointly optimize ship assignment and allocation. Transfers can be too large to carry in one mission. Forecasts do not include a calibrated population survival model.
- Flight totals charge the full planned leg on departure, including when a ship is later destroyed. Construction applies resource production changes at completion. Population and energy effects are modeled as fields but are not applied.
- The generated scenarios are useful for controlled scaling. Imports of external scenarios, full uncertainty modeling, and the full report required by the professor remain future work.

## Algorithm notes

Let `V` and `E` be location and directed route counts. Dijkstra visits a vertex only when its best known distance is popped. With all admitted edge costs finite and nonnegative, a cheaper route to that vertex would have required a smaller frontier entry and would have been popped first. This also justifies stopping at the first settled eligible donor in the reversed graph. The easy case settles a single vertex. In the worst case, lazy heap entries give `O((V+E) log(2+E))` time and `O(V+E)` scratch memory. Edge updates are expected constant time by ID. A reverse adjacency graph takes `O(V+E)` setup time and storage.

The route-aware allocator removes exhausted donors and runs at most one nearest-donor query per transfer plus one unsuccessful query per receiver. It preserves stock bounds for valid nonnegative inventories in its planning model. Its priority order and nearest-supplier decisions are local choices, so they can leave later receivers underserved. The basic allocator scans suppliers directly and is quadratic in demand rows in the worst case.

Scheduling sorts `M` missions in `O(M log M)` and stores ships by projected location. If a mission has local candidates, it performs at most three route queries plus candidate checks. Stale heap entries, rejected missions, and failed alternative routes still consume work. The simulator revalidates the exact edges and physical constraints before departure and releases each ship at its actual arrival. This prevents overlap even when a route change delays a trip. Event queue operations are logarithmic in the number of queued actions. Route recomputation adds up to three Dijkstra queries per invalidated departure.

Knapsack uses a descending budget loop. Before considering a project of cost `c`, the state for `b-c` still represents earlier projects (for positive `c`). Each update chooses the better value of excluding or including the project. A zero-cost project updates each state once. For `P` projects and an integer budget `B`, the recurrence costs `O(PB)` time, and reconstruction records can use `O(PB)` space. Exhaustive search grows exponentially with the number of projects. The scheduler also makes coupled assignment and sequencing decisions and provides a feasible greedy schedule under the modeled constraints, with no proof of global optimality.

