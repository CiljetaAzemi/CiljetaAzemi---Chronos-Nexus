# Chronos Nexus

## Abstract

Chronos Nexus is a C++20 simulation and planning system for allocating resources, scheduling spacecraft, selecting infrastructure, and responding to disruptions in a directed galaxy network. It combines weighted shortest-path search, greedy allocation and scheduling, and budget-constrained dynamic programming. In the recorded Release x64 evaluation, the system completed the small, medium, and large scenarios. The extreme scenario reached the 600-second limit during scheduling. The large run completed 2,150 of 5,502 input and generated aid missions.

## Problem formulation

Given a directed network, initial inventories and their production and consumption rates, a fleet, requested missions, and a construction budget, the system produces resource transfers, ship assignments, and a construction plan. A discrete-event simulator evaluates those decisions against disruptions, stock levels, and actual ship availability.

Locations include planets, colonies, and stations. Each directed edge has an identity, distance, travel time, fuel requirement, cargo limit, risk, and reliability. Parallel edges remain distinct. Travel times are expressed in days at a reference speed and divided by the assigned ship's speed multiplier. Operating cost is route distance multiplied by the ship's cost per distance unit. Risk is an additive exposure index. Inventories are indexed by location and resource type, with independent capacity and production and consumption rates.

The implementation treats cargo as indivisible within a mission. A planned departure requires a ship at the origin with sufficient fuel and cargo capacity, an eligible route, and an arrival no later than the deadline. Execution additionally checks origin stock and actual ship state. Construction selection uses an integer budget and additive strategic value.

## System design

| Stage | Method |
|---|---|
| Routing | Dijkstra search minimizes a weighted sum of distance, fuel, reference travel time, risk, and a reliability penalty. Edges that are blocked or cannot carry the mission cargo are excluded. The result retains exact edge IDs and separate physical totals. |
| Resource allocation | Receivers are processed by priority. A search on the reversed graph finds the cheapest eligible supplier for each transfer. The logistics planner converts transfers into aid missions and can shorten their deadline using projected stock depletion. |
| Mission scheduling | Missions are considered by descending priority and then earliest deadline. The scheduler evaluates ships at the projected origin and updates the selected ship's projected state. The custom ranking balances normalized travel time, unused cargo capacity, and the fraction of tank capacity consumed, with weights 0.50, 0.35, and 0.15. When the weighted route yields no feasible assignment, the scheduler also tests minimum-fuel and minimum-time routes. |
| Infrastructure | A descending-budget 0/1 Knapsack recurrence selects projects under the construction budget. Scheduled construction completions can increase resource production. |
| Event simulation | A time-ordered queue processes disruptions, departures, and arrivals. The simulator validates exact routes before departure and attempts a new route if the planned route is infeasible. Ship location and availability follow actual arrivals. |

## Correctness and complexity

Dijkstra's early exit is valid for finite, nonnegative edge costs. The same argument applies to the first eligible supplier settled in the reversed graph. The allocation step respects each supplier's modeled surplus, and a local choice can consume the only supplier available to a later receiver. The scheduler applies feasibility checks to each assignment and uses a restricted route set and a greedy mission order. The simulator prevents overlapping flights by checking actual ship availability. Knapsack compares the value of including or excluding each project. Descending budget iteration prevents reuse of positive-cost projects, and each zero-cost project updates each state once.

Let **V** and **E** denote locations and edges, **M** missions, **P** projects, and **B** integer budget units.

| Component | Cost |
|---|---|
| Graph | O(V + E) storage. Edge lookup and update by ID take expected O(1) time. |
| Dijkstra | O((V + E) log(2 + E)) worst-case time and O(V + E) auxiliary space with lazy heap entries. Early termination can visit substantially fewer vertices. |
| Route-aware allocation | O(V + E) to construct the reversed graph, plus receiver sorting and at most one shortest-path query per transfer and one unsuccessful query per receiver. |
| Scheduling | O(M log M) for ordering, followed by up to three route queries per mission with local candidates and the corresponding ship checks. |
| Simulation | O(log A) per queue operation for A queued actions. Route repair adds up to three route queries per affected departure. |
| Infrastructure | O(PB) recurrence time and up to O(PB) reconstruction storage. The bound is pseudo-polynomial in the budget. |

The regression entry point checks route identity, capacity, fuel, overlapping departures, directed allocation, shortage prediction, inventory events, and Knapsack against exhaustive subset enumeration on a small instance.

## Evaluation objective

The benchmark evaluates simulated outcomes using the following unnormalized score.

```text
10 × completed − 10 × failed − 5 × rejected + delivered
− ending shortage − unserved demand − waste
− 0.01 × transportation cost − risk + construction strategic value
```

Completed and failed missions are execution outcomes. Rejections occur during planning. Delivered cargo is the quantity accepted at destinations, ending shortage is the deficit against stock targets, and unserved demand accumulates when inventories are depleted. Waste includes overflow and cargo lost in flight. Construction contributes the assigned strategic value of selected projects.

The weights define an experimental trade-off. Mission completion, cargo delivery, and shortage reduction can all reflect the same shipment. Generated aid also changes the set of missions exposed to the rejection penalty. Population survival, economic growth, scientific progress, territorial expansion, energy use, and computational cost have no direct terms. Runtime, memory, and fuel are reported independently. The competition coefficients and evaluation procedure are unspecified.

## Findings and limitations

The recorded comparisons show higher delivered quantity for route-aware allocation on all three completed sizes and modest completion gains for the custom scheduler on medium and large. Conditional route repair reduced the number of route searches under paired closures while preserving the measured delivery and completion outcomes. Scheduling dominated the large run and was the stage at which extreme timed out. Detailed values, evidence, and run commands are in the [experimental evaluation](ChronosNexus/benchmark-results/README.md).

The current method can miss feasible routes outside its tested route set. It does not reposition or refuel ships, or reassign missions after ship loss. Route closures affect pending departures, and supply shocks change inventories without creating new missions during execution. Aid deadlines use initial depletion estimates. Edge reliability influences ranking, while independent travel-time and failure variability is outside the current simulation. Construction production effects are modeled, while population and energy impacts are outside the evaluation.

### Optional inventory-aware scheduling

The `inventory` mode reserves stock against nominal production, consumption, and committed deliveries. One append-only retry pass can recover rejected missions and test up to four alternative suppliers for an aid obligation. The `custom` mode remains the default. Use `inventory-uncached` to isolate the effect of route reuse.

Inventory-aware scheduling can improve timely service while increasing aggregate ending shortage when stock and ships are scarce. It does not protect a supplier's target stock as a hard reserve, revise previously accepted assignments, or recover every dependent chain in a single retry pass. Unexpected production cuts can invalidate nominal reservations. Subsequent optimization should evaluate completion, weighted timely fulfillment, ending shortages, and fuel together on fixed obligations.

The optional mode caches exact forward shortest-path results within one planning call. The key includes endpoints, cargo, and all route weights. The cache admits up to 12,288 entries with a combined 2 MiB limit on allocated path-vector payload. Container and entry overhead are separately bounded by the entry limit. Changes to graph topology, route availability, or risk invalidate the cache. Reverse supplier searches remain live because their eligible donors depend on current ship locations.

### Experimental solution improvement

The `phase2` mode improves a cached inventory-aware plan through bounded, dependency-aware local search. It evaluates changes to ship tails and the affected inventory accounts, including fuel and deadline constraints and guards against increases in forecast shortages. The [algorithm note](ChronosNexus/PHASE2.md) provides pseudocode, feasibility arguments, complexity, and limits. The [experimental evaluation](ChronosNexus/benchmark-results/README.md) gives the test evidence and reproduction commands.

## Build and reproduce

Use the [Visual Studio build and troubleshooting guide](ChronosNexus/BUILD.md) to build Release x64 and run the regression suite. The [experimental evaluation](ChronosNexus/benchmark-results/README.md) defines the datasets, variants, resource limits, and output fields.

### External JSON scenarios

Open `ChronosNexus/ChronosNexus.slnx` in Visual Studio and select **Release | x64**. In **ChronosNexus > Properties > Configuration Properties > Debugging**, set **Working Directory** to `$(ProjectDir)` and **Command Arguments** to the following, then press **Ctrl+F5**.

```text
--scenario-file "$(ProjectDir)examples\scenario.json" --output "$(TargetDir)scenario-result.json"
```

Use the [example scenario](ChronosNexus/ChronosNexus/examples/scenario.json) as a template. Set `"version": 1` and provide `budget` and the `locations`, `routes`, `inventory`, `spacecraft`, and `missions` arrays. The horizon defaults to 30 simulation days. `projects` and `events` are optional. Routes are directed. Every input mission needs a matching resource account at both endpoints. Mark both donor and recipient inventory accounts with `"aid": true` to allow automatic aid proposals for that resource.

Distances, fuel, cargo, and operating costs use the scenario's consistent units. Time is measured in simulation days, production and consumption in resource units per day, and construction cost and budget in whole credits. Supported resource names are `food`, `water`, `energy`, `fuel`, `medicine`, `rare_minerals`, and `construction_materials`. The simulator accepts `route_blocked`, `wormhole_collapse`, `pirate_blockade`, `route_risk_increase`, `ship_destroyed`, `ship_failure`, and `production_decrease`. A production decrease can be marked `"announced": true` to include it in the forecast. Construction projects can increase food production.

The JSON mode uses the existing civilization planner with the balanced objective profile. The result's `planned` section reports proposed allocations, committed assignments and route IDs, selected construction, and a nominal forecast. The `simulation` section reports realized deliveries, mission status, resource outcomes, and aggregate costs and replanning. A planning rejection appears in `rejected_mission_ids`. Its simulator status may remain pending. Realized route traces are unavailable from the simulator. The output file is written to the build directory in the Visual Studio example above. Successful runs produce the report file without printing a console summary.

### Dynamic replanning

The simulator repairs pending assignments after a spacecraft becomes unavailable or an exact route edge closes. A route delay can also invalidate later bookings on the same spacecraft. When an `urgent_mission` or `emergency_medical_request` event arrives, its nested `mission` uses the same fields as an input mission and becomes eligible at the event time. Mission IDs are unique across initial and urgent missions. Medical requests must carry medicine.

The simulator keeps pending actions indexed separately from completed and in-flight missions. For each invalidated assignment or new request, it checks local spacecraft slots, reserved fuel, and the two affected resource accounts. Candidates compete over the existing weighted, minimum-fuel, and minimum-time paths. Fixed bookings keep their departure time, route, and spacecraft when feasible. A successful repair reserves cargo and storage over time before the new departure is queued. This is a bounded heuristic. It can miss feasible alternatives that require an empty repositioning trip, a different path from the three searches, or moving an unrelated booking. A later event can invalidate a successful repair.

A spacecraft event scans the pending list. A route event additionally checks each pending path for its exact route ID. For one repair attempt with `P` pending bookings, sorting ship bookings takes at most `O(P log P)`. The attempt measures existing booked paths, makes at most three shortest-path searches, and checks a bounded number of departure times per compatible slot against the affected inventory timelines. It does not regenerate the civilization plan.

In the Visual Studio JSON command above, replace `examples\scenario.json` with `examples\replanning.json` and the output name with `replanning-result.json`. Inspect `simulation.replanning_decisions` for the triggering event, previous assignment, and replacement. A null replacement means recovery found no feasible assignment. Compare with `simulation.missions` for the final outcome.

### Proactive shortage prediction

JSON scenarios forecast each resource balance to the specified horizon. The inventory timeline applies production and consumption between scheduled and in-flight deliveries, departures, announced production changes, and selected infrastructure completions. With constant net consumption, depletion occurs after `stock / (consumption - production)` days. A delivery that arrives exactly as stock would reach zero can cover demand without a gap. Ending shortage measures stock below target at the horizon. The forecast also reports unmet demand or planned cargo withdrawals during the horizon.

When a forecast finds depletion, the simulator uses the existing recovery planner to find deliveries due strictly before that time. A priority queue orders all eligible suppliers by projected surplus and then by location ID. The planner checks spacecraft availability, cargo and route capacity, fuel, deadlines, storage, reservations, and donor security. It searches for cargo that fits the selected route and spacecraft, reducing the quantity when inventory constraints require it. A delivery must improve the recipient forecast. After accepting it, the planner reserves the delivery, recalculates the remaining need, and updates only the selected supplier's priority. Suppliers can contribute repeatedly while they have surplus and a feasible spacecraft slot. The search stops when the forecast clears or no remaining candidate can improve it. Each accepted trip uses a distinct slot for this destination and consumes surplus, so no supplier or mission count limit is needed.

A production event refreshes forecasts for the changed accounts. Scheduled deliveries already count toward the forecast and reduce the remaining need. The planner retains the existing weighted, minimum-fuel, and minimum-time route searches and the existing departure-time candidates. It can miss feasible plans that need a different route, an empty repositioning leg, or moving an existing booking. Historical benchmark modes retain their existing configuration.

For an account with `K` inventory events, building its ordered timeline takes `O(K log K)` time and `O(K)` space. A forecast sweeps that timeline in `O(K)` time. The simulator collects pending and in-flight inventory entries once per checkpoint. For an at-risk account with `D` eligible suppliers and `M` accepted deliveries, donor queue operations cost `O((D + M) log(D + 1))` time and `O(D)` space. Each acceptance adds at most one new supplier evaluation, so there are at most `D + M` replanner calls. Each call checks spacecraft bookings and runs up to six shortest-path searches using the requested and minimum meaningful cargo quantities. When a trial cannot carry its full quantity, bisection requires `O(log(U / ε))` inventory checks per candidate departure, where `U` is the cargo upper bound and `ε` is the precision derived from the scale of the account balances.

Run the [shortage example](ChronosNexus/ChronosNexus/examples/shortage.json) with `--scenario-file "$(ProjectDir)examples\shortage.json" --output "$(TargetDir)shortage-result.json"`. Inspect `planned.forecast.shortage_prediction` for the nominal forecast and `simulation.shortage_prediction` for forecasts and preventive decisions during the actual simulation. Each decision includes before and after projections and the assignment, if feasible. The example starts with the specification’s 10,000 food units and net consumption of 400 units/day, for a day-25 depletion forecast before intervention.

### Route uncertainty

JSON routes use `travel_time` for expected travel time at reference speed. Optional `travel_time_min` and `travel_time_max` default to that time. Bounds must be nonnegative and enclose the expected time. Supply either `reliability` or `failure_probability`, both in `[0, 1]`. The default is reliability 1. Routes with certain failure are excluded. The `risk` field remains independent.

Path times add across edges. The existing time weight uses the upper bound, and the existing reliability weight penalizes the sum of edge failure probabilities. Under independent failures, the exported route reliability is the product of edge reliabilities. It is separate from the existing additive risk score. The scheduler and the replanner divide the upper travel time by spacecraft speed when checking deadlines and reserving spacecraft and cargo. The simulator follows the same conservative timing and handles disruptions through events. It does not sample travel times or generate random failures.

JSON flight records report estimated earliest, expected, and latest arrivals, route reliability, failure probability, and risk. `scheduled_arrival` is the conservative time used by the plan. The upper-bound deadline guarantee applies while travel stays within the supplied bounds and the route and other resources remain available. This policy can reject a mission that would meet its deadline in faster realizations. The change adds constant work per edge and preserves the asymptotic costs of route search, scheduling, and replanning.

### Cost-aware civilization planning

The JSON workflow and `phase3` rank candidate missions by cargo-weighted priority and importance and include operating cost and spacecraft reliability when comparing feasible assignments. If importance is declared, the planner also evaluates a priority-first candidate. The local improver can replace a terminal booking with a rejected mission and a feasible successor. Each move rechecks ship state and affected inventory accounts. The civilization planner evaluates the resulting complete candidates using the existing objective.

Aid proposals use the priority-ordered receiver allocator by default. The alternative ranking is experimental and can be selected with `"experimental_allocator": true` in scenario JSON or `--experimental-allocator` after the benchmark mode. The downstream scheduler checks shipment capacity, inventory, and deadlines. A risk increase can trigger a targeted route comparison.

The realized civilization score includes initial missions and every external urgent request activated within the horizon. Failed requests receive zero service credit. Aid and preventive supply missions affect inventory and operating costs without expanding the service obligation set. The nominal forecast uses the initial obligations. When an urgent request cannot use spare capacity, the replanner can compare replacing a terminal booking against retaining it. Both sides use the same objective and obligations known at event time, including shared projected bookings, inventory, construction, and incurred costs. A replacement must pass inventory, spacecraft, and upper-bound deadline checks and strictly improve the forecast score. The comparison assumes other reservations execute as planned, so its improvement applies to the event-time forecast.

An urgent request that fits spare capacity needs one recovery search. Otherwise, the simulator identifies terminal bookings in `O(P log S)` time for `P` pending bookings and `S` spacecraft and can try at most one terminal booking per spacecraft. It builds the common forecast once when a feasible replacement is found. Later trials replay only the changed inventory accounts, while scoring scans the known obligations and critical resource outcomes. Working storage is linear in the number of accounts, timeline entries, bookings, and obligations, plus the existing route-search workspace.

The experimental allocator's scarcity regressions were traced to the proposal stage. An earlier allocation to cheaper recipients could leave too little proposed cargo for a critical colony, even when the later scheduler found the original spacecraft and route. The scheduler reserves the amounts in the proposals, which limits what it can recover by switching suppliers. The default uses the priority-ordered allocator while ship-aware proposal feasibility and critical-demand protection remain topics for further evaluation.

### Experimental feasibility-aware allocation

Select the new mode with `"feasibility_aware_allocator": true` in JSON or `--feasibility-aware-allocator` in Phase 3. Phase 3 first schedules the fixed obligations and selects construction. The aid pass builds stock timelines from the resulting bookings and known production changes. It considers critical colony shortages first, then other essential resources, depletion time, and priority. Donors must have projected surplus and a spacecraft that can reach their location through current bookings. The existing recovery planner tests the routes, spacecraft, amounts, and departure times. Only a feasible partial or full shipment commits to the trial timeline. A failed probe leaves inventory and spacecraft bookings available to the rest of the pass.

The planner replays the augmented candidate once and retains it only if every committed shipment arrives in full by its deadline and the existing objective improves. This protects the fixed obligations under the nominal event stream. Unexpected disruptions use the existing targeted repair. The aid pass does not displace fixed bookings or rerun its allocation after every event.

Within the aid pass, validated spacecraft tails are indexed by location. A non-loop shipment cannot precede a fixed booking because the fixed booking starts where the previous leg leaves the ship. An inserted shipment would move it to a different location. The original feasibility checks therefore admit only tail insertions for this pass. The index projects each existing itinerary once and appends only the newly accepted leg on the affected ship. It keeps the old ship iteration order for deterministic quantity and tie handling. The index lives only inside the day-zero transaction, where the graph, clock, and original bookings are fixed. The runtime replanner uses its original event-time validation logic. Use `--legacy-aid-search` with `--feasibility-aware-allocator` to reproduce the previous full-ship search.

For `A` inventory accounts and `K` timeline changes, building the starting projections takes up to `O(A + K log K)` time and `O(A + K)` space. For each recipient, screening donors scans the relevant accounts and timelines, then orders eligible donors by surplus. A failed donor probe retires that donor for this recipient. A success reduces the projected deficit and consumes a feasible spacecraft slot, so the donor can be reconsidered only while further physical progress is possible. Worst-case time adds a donor scan for each recipient, the sum of recovery probe costs, and one nominal replay. Each recovery probe checks pending bookings, uses the existing weighted route searches, and can bisect cargo to the existing numerical precision. The route and departure-time heuristics can miss feasible combinations, and the final replay can reject the whole batch when a smaller batch would improve the score.

See [decision regressions](ChronosNexus/ChronosNexus/tests/Test-DecisionMaking.ps1) for controlled comparisons of the old and new executables. The script takes `-Executable` and optionally `-Baseline` and reports JSON engine time and the observed process peak. The in-process suite is available through `--self-test`. The [fixture results](ChronosNexus/benchmark-results/decision-making-fixtures.csv) and [paired Phase 3 benchmark results](ChronosNexus/benchmark-results/decision-making-benchmarks.csv) record the measured quality and execution costs.

## Project structure

The Visual Studio solution and benchmark runner are in `ChronosNexus/`. The C++ project is in `ChronosNexus/ChronosNexus/`.

| Location | Responsibility |
|---|---|
| `main.cpp` | Dispatches the demonstration, benchmarks, regression tests, and external scenario mode. |
| `models/`, `graph/` | Domain records and the directed weighted graph. |
| `algorithms/`, `simulation/` | Planning algorithms and discrete-event execution. |
| `benchmarks/` | Benchmark orchestration, deterministic dataset generation, and measurement and reporting utilities. |
| `tests/` | Regression suites for routing, simulation, allocation, inventory, scheduling, construction, and plan improvement. |
| `examples/` | Console demonstrations and an example JSON scenario. |
| `scenario/` | Validates external scenarios and exports plans and simulation outcomes. |
| `ChronosNexus/benchmark-results/` | Archived CSV evidence and regression summaries. New runs default to the ignored `runs/` subdirectory. |

## Experimental civilization objective

The `phase3` mode compares a bounded set of joint logistics and construction plans. The earlier modes remain available, and `custom` remains the default. The historical civilization score is retained for continuity. Its raw terms combine unlike scales and can reward the same shipment through completion, delivery, and a smaller shortage. Phase 1's scarce-fleet test illustrates the disconnect. Timely fulfillment improved while ending shortage rose. Phase 2 also increased realized shortage by 36 units on the small continuity seed despite its nominal guard.

For a nonnegative quantity `x` and fixed scenario reference `r`, define `p(x,r) = x / (x + max(1,r))`. The objective uses the following dimensionless components.

| Component | Definition |
|---|---|
| Security `S` | One minus the mean of normalized ending shortage and cumulative unserved demand. References are initial stock targets and horizon consumption, respectively. |
| Critical security `K` | The same calculation restricted to colony food and medicine. This is a resource-stability proxy. It does not estimate survival or population change. If no colonies exist, use overall security. |
| Service `F` | Priority-weighted accepted input cargo delivered by its deadline divided by priority-weighted requested input cargo. All original requests remain in the denominator. Generated aid enters through the inventory consequences; all-obligation fulfillment is also reported separately. |
| Infrastructure `I` | Selected strategic value divided by the total nonnegative strategic value of the offered projects. Production impacts are simulated separately; strategic value is treated as a proxy for other benefits. |
| Operational penalty `O` | Mean of normalized transport credits, fuel, and construction spending. References are one credit and one fuel unit per requested input cargo unit, and the construction budget, respectively. |
| Waste and risk `W, R` | Normalized waste and additive route risk with references of requested cargo plus horizon consumption, and one exposure unit per input mission, respectively. |

The balanced score is `30S + 25K + 20F + 5I − 10O − 5W − 5R`. Security receives 55 of 100 weight points to emphasize the project's resource-security objective. The critical slice gives essential colony stocks additional emphasis. Service receives 20 points, construction strategic value receives five, and the remaining points penalize operations, waste, and exposure. These are policy preferences with fixed reference scales, and the official competition coefficients remain unknown. The security profile uses `35,35,10,5,5,5,5`, while the service profile uses `20,15,40,5,10,5,5`. Custom coefficients can be supplied as seven nonnegative values with `--objective=` and are normalized to sum to 100. Zero-denominator service and infrastructure components contribute zero.

### Planning heuristic

```text
seed ← cached inventory-aware schedule on fixed obligations
standard construction ← budgeted strategic-value Knapsack
targeted construction ← Knapsack with projected production-deficit bonuses
propose the seed with standard construction and with no construction
propose the Phase 2 local-search result with standard construction
for standard and targeted construction:
    reprioritize obligations using projected inventory and construction timing
    shortlist alternate donors for up to 24 aid obligations
    generate a feasible schedule with one of the existing ship rankings
simulate each candidate with committed construction and without surprise events
discard candidates with nominal execution failures, clipped cargo, or late deliveries
select the feasible candidate with the greatest civilization score
```

Fixed obligations retain their identities, destinations, quantities and deadlines. Their original priorities define the evaluator. A candidate can change solver priorities, suppliers, and ship choices. Nominal replay includes committed construction and can receive explicitly announced production cuts. The benchmark passes no future disruption events to the planner. Unexpected route or production changes are supplied to the final simulation and remain outside this phase's planning model. The simulator enforces the physical and stock conditions, and Knapsack bounds construction spending. If no candidate passes, the planner can return an empty feasible plan and all obligations remain unserved for scoring.

The original techniques are Dijkstra, greedy scheduling, 0/1 Knapsack, and event simulation. The Phase 3 contribution is the fixed-denominator score, deficit-based construction and supplier proposals, and joint selection by replay using known production completions. The heuristic tests five joint candidates and at most one empty fallback. With scheduler cost `A`, local-search cost `L`, nominal replay cost `E`, shortest-path query cost `D`, and `P` projects and budget `B`, additional planning time is bounded by `O(2A + L + 6E + 48D + PB)`, plus inventory indexing, obligation sorting, and project lookup. The candidates are evaluated sequentially. Storage holds a bounded number of schedules and inventory/graph snapshots, plus the existing scheduler, search, and Knapsack workspace.

The proposal budget can miss better supplier combinations, time-phased transport, or construction portfolios. Candidate generation uses approximate deficit priorities and the existing freight planner. The final nominal replay checks timed construction effects exactly as the simulator models them. The [benchmark evaluation](ChronosNexus/benchmark-results/README.md) reports sensitivity and realized outcomes.
