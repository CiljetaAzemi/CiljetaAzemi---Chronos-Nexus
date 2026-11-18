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

The current method can miss feasible routes outside its tested route set. It does not reposition or refuel ships, reserve cargo during scheduling, or reassign missions after ship loss. Route closures affect pending departures, and supply shocks change inventories without creating new missions during execution. Aid deadlines use initial depletion estimates. Edge reliability influences ranking, while independent travel-time and failure variability is outside the current simulation. Construction production effects are modeled, while population and energy impacts are outside the evaluation. The next quality evaluation should test inventory-aware scheduling on fixed demand obligations before comparing broader plan-improvement methods.

## Build and reproduce

Use the [Visual Studio build and troubleshooting guide](ChronosNexus/BUILD.md) to build Release x64 and run the regression suite. The [experimental evaluation](ChronosNexus/benchmark-results/README.md) defines the datasets, variants, resource limits, and output fields.
