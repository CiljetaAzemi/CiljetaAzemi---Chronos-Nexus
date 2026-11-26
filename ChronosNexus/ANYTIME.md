# Computational budgets and anytime planning

Requirement 18 asks for a solution within a predefined time and memory budget. The competition rules describe a common budget and leave its values and enforcement procedure open. The experimental limits below are project settings.

## Run the planners

In Release x64, set the arguments under **Project Properties → Debugging → Command Arguments**. Omit the budget options for the existing consolidated Phase 3 path. The legacy objective remains the default in both paths.

```text
--benchmark large 20261120 phase3 --time-budget-ms=35000 --memory-budget-mib=1024
--benchmark medium 20261201 phase3 --scenario=dynamic --time-budget-ms=1000 --expanded-objective
--scenario-file INPUT.json --output RESULT.json --time-budget-ms=1000 --memory-budget-mib=1024
```

Each budget option takes a positive integer. A time limit and a working-set threshold can be supplied independently. By default, the time reserve is 15% of the limit, with a floor of 1 ms, a ceiling of 3 s, and a cap of 90% of the total. Use `--reserve-ms=N` to choose a reserve smaller than the time limit. During the reserve the engine may validate the last scheduler prefix and run a complete event simulation. These operations stop at a second cutoff that leaves one quarter of the reserve for reporting. Output is serialized once the planner yields. JSON parsing and dataset generation count toward elapsed time, but must finish before the first planning checkpoint. The elapsed value inside JSON is measured before serialization. The benchmark study separately measures end-to-end process wall time.

## Algorithm

```text
replay the empty plan against the fixed obligations
construct an external-only inventory-aware schedule
    after 1, 2, 4, ... committed assignments, replay that prefix
    publish it if valid and strictly better under the active objective
replay the completed external-only schedule
if planning time remains, generate ordinary aid and reschedule if needed
try existing Phase 3 portfolios, local moves and alternative schedules
optionally try indexed feasibility-aware aid with existing reverse bounds
if interrupted during initial scheduling, try validating the returned prefix in the reserve
if possible, simulate the best validated incumbent against the event stream
export the last validated plan and separate completion flags
```

The first stage is external-only so a slow allocator cannot precede every feasible mission. The initial scheduler uses the same capacity and upper-bound travel-time checks, ship location/fuel state, and inventory and storage reservations as the consolidated planner. It exposes prefixes only after a complete booking. Each candidate is replayed with announced production changes and its construction events. Route availability, simulated deliveries and deadlines are checked before publication. Optional aid and infrastructure remain private to the candidate until validation and score comparison finish. The fixed set of external obligations is used for all nominal re-scores. Unscheduled IDs remain visible and earn zero service credit. Activated urgent requests enter both realized formulas only in completed simulation.

The selected objective must strictly increase on successive accepted incumbents. Feasible prefixes can lose to the empty plan if their operating burden exceeds their modeled benefit. The first nonempty timestamp records the first validated nonempty candidate even if it loses that comparison. The other formula and raw quantities can worsen while the active score improves. A future critical-shortage floor would make that trade-off explicit, but needs a defined reference and exception policy for unavoidable shortages. Comparing Pareto dominance is useful for screening candidates. It cannot by itself choose among incomparable plans. Neither policy is enabled. A generous budget can return a different result if a new prefix wins or ties a legacy candidate. The budget-aware path deliberately retains that prefix.

The forecast is the nominal replay used to validate the returned plan. In JSON, `planning_complete` means the static candidate pipeline finished. It is independent of `all_external_assigned`. A forecast of `null` means there was no validated incumbent. A simulation of `null` means there is no complete event-stream outcome. Unknown events can invalidate previously valid decisions and change realized quality; the simulator applies its targeted recovery when time permits.

## Interruption and resource accounting

A scoped budget uses a monotonic `steady_clock`. Checkpoints in scheduling, route searches, inventory timelines, allocation, Knapsack, local search, event processing, and recovery unwind the active operation. The incumbent owns copies of the published schedule and forecast. Unwinding a replay or an optional candidate discards its local state. The initial scheduler can return its committed prefix after an interrupted probe. The optional allocator or local-search call can lose work since its last publication. No mid-simulation state is exported as a realized outcome.

On Windows the planner samples current process working set at cooperative checks, at most once per 20 ms, and reports the OS peak. Working set is resident process memory, including data outside the planner. It can change as the OS pages memory in or out. Estimated algorithmic memory from graph, cache, and candidate structures is useful for complexity analysis but differs from resident pages and committed memory. A sampled threshold cannot enforce a process cap. The benchmark runner adds an external watchdog with a 1,024 MiB sampled peak limit. A hard competition limit would need an external sandbox or Windows Job Object with a specified accounting metric. Neither monitor guarantees recovery after an allocation failure.

Graph copies, sorting, container growth or destruction, individual library calls, input parsing or generation, and output serialization and I/O can delay the next checkpoint or final return. The reserved window helps protect output time. OS scheduling and slow storage can still cause overshoot. An extremely small budget may produce only diagnostics and the visible unassigned obligation set.

## Complexity

Let `M` be input missions, `A` accounts, `V,E` the graph dimensions, and `P` the largest retained schedule payload including route IDs. Let `S` be the scheduler cost, `R(k)` nominal replay cost for `k` legs, and `C` existing Phase 3 optimization cost. Prefix validation runs at most `O(log(M+1))` times, plus constant full-prefix or final attempts. Its cost is the sum of replay costs. Each replay currently rebuilds its state from the graph, accounts and obligations. The geometric series bounds the sum of prefix legs by `O(M)` across the geometric checkpoints, but shared replay setup can cost `O((V+E+A+M) log(M+1))` in total. The additional scheduler pass can cost `O(S)` when aid causes rescheduling. The full pipeline has cost bounded by `O(S + C + log(M+1) R(M))` plus the final simulation. If preprocessing consumes the entire time, the planner can terminate before its first replay. There is no input-independent average performance guarantee.

Beyond existing transient workspaces, the planner retains one incumbent, one prefix while trying alternatives, and a trace with logarithmically many prefix entries plus the existing finite candidate set. This adds `O(P + A + M)` storage for schedule, outcomes and job metadata, plus trace entries. A checkpoint has constant overhead, with an infrequent OS query under a memory budget. The memory and time limits are cooperative. Feasibility is established for accepted decisions under the nominal inputs. The planner does not establish global optimality or full service.

The scheduler already reuses ship and reservation state between prefix checkpoints. The local improver already checks only affected ship tails and inventory accounts inside a move. Incremental validation across those static moves could reuse unchanged route and inventory results if dependencies and capacity effects were tracked across affected accounts and ship tails. Full replay remains the final feasibility check here. The Large measurements show that replay was a small share of runtime. Avoiding repeated scheduling of the external missions would address a larger observed cost.

With `--security-priority`, monotonicity applies to the lexicographic key in [the objective report](OBJECTIVE.md). Every accepted static incumbent has a strictly larger key than its predecessor. Either historical scalar score may decrease. Logs include essential coverage and critical unserved consumption alongside the existing critical stock and service data, so the key can be reconstructed. Dynamic events may change feasible assignments and activate new obligations after static planning.

[Benchmark methodology, curves, and reproducible measurements](benchmark-results/README.md)
