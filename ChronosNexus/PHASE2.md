# Dependency-aware terminal local search

The experimental `phase2` benchmark mode starts from the cached inventory-aware plan. The `custom` and `inventory` modes remain independent controls. All three receive the same initial state and the same generated obligations. The architecture keeps the original allocator and simulator.

## Neighborhood and objective

The search indexes projected ship tails, terminal origins, and rejected missions by destination. A rejected mission arriving at another rejected mission's origin is a candidate predecessor. Moving cargo and the ship there can enable the next leg. The search considers direct insertions, predecessor–target pairs, terminal replacements, returning pairs inserted before a terminal, terminal relocations, and exchanges between terminal legs of two ships. A terminal replacement can reassign the displaced mission to another ship. Committed aid supplier choices are fixed during this search.

For each candidate, only the affected ship tails and inventory accounts are copied and checked. Earlier ship prefixes and unrelated stock accounts are retained. The two guarded forecast metrics are total ending shortage against targets and cumulative unserved demand. A move must keep each from increasing, within absolute numerical tolerance of `1e-8`. Among qualifying moves, the objective is to increase:

```text
sum(max(0, priority) × timely planned cargo) − 0.01 × operating credits − additive route risk
− 2 × ending shortage − 2 × unserved demand
```

All new deliveries must fit entirely in planned destination storage and meet their deadlines. This is a nominal optimization surrogate under constant production and consumption rates. Unexpected route closures, ship losses, or production changes can still invalidate reservations at execution. The benchmark reports actual outcomes and the existing civilization score separately.

## Pseudocode

```text
incumbent ← cached inventory-aware plan
index ship tails, rejected predecessors, and inventory reservations
if nominal inventory replay rejects incumbent, return incumbent

for up to 64 priority-ordered rejected obligations:
    consider a bounded list of compatible ship tails and terminal origins
    try direct insertion, enabling pair, or terminal replacement/resequence
    commit the first improving feasible move, if any

inspect up to 24 terminal legs for relocation or exchange
return incumbent

TRY MOVE:
    copy touched inventory accounts and affected ship tail states
    remove the named terminal reservations from the copies
    for each proposed new leg in order:
        search for bounded fuel/time-feasible route labels
        choose the first route and departure that pass mission and stock checks
        reject the move if no such choice is found
    replay touched accounts and check shortage guards and objective improvement
    atomically replace the affected tails and ledgers if all checks pass
```

The move budget is 192 trials, with at most 144 used for rejected obligations so that terminal relocation and exchange can still be considered. Origin lookups return up to three ships, and a chain or returning-pair search considers up to three compatible predecessors. A route query expands at most 256 labels, inspects at most 4,096 arcs, retains at most eight active labels per vertex, and returns at most three complete routes. It prunes blocked or undersized edges, labels outside fuel and time limits, and labels dominated in fuel, time, and weighted cost. Exceeding the label cap drops alternatives. Candidate departure times use the existing bounded inventory forecast. Unexplored paths and moves are outside the search's guarantees.

## Feasibility and termination

Assume a feasible starting schedule for the same nominal graph and inventory forecast. A move removes only terminal legs, so unaffected ship prefixes remain feasible. Each appended leg starts from the actual projected location and remaining fuel of that prefix and is checked for availability, speed, reliability, ship capacity, edge capacity, fuel, and deadline. The affected ledger includes all retained reservations for those accounts, even if the corresponding ships are unaffected. Replaying it checks inventory and capacity across the entire affected timeline. Failed trials leave the incumbent unchanged. Successful trials improve the guarded nominal objective and replace only the affected state. The deterministic budgets ensure termination.

For a fixed ship and nonnegative edge terms, a label that already exceeds fuel or remaining time cannot become feasible by extension. At the same vertex, a label with no greater fuel, time, and weighted cost can match another label's extensions as far as those route quantities are concerned. This does not prove that its arrival timing or route better serves inventory and the system score. The label cap, finite route portfolio, restricted moves, and first-fit scheduling rule preclude a global optimality guarantee.

## Exact reference cases

- The dependent three-leg chain and returning-pair fixture each have three obligations. An explicit feasible schedule serves all three, which reaches the completion upper bound.
- In the single-obligation route fixture, the three parallel route choices have fuel/time pairs `(1,12)`, `(12,1)`, and `(6,6)`. Only the third meets both six-unit limits. The constrained search recovers that route.
- The terminal exchange fixture has two feasible ship–mission pairings with costs of 501 and 105 credits. Comparing the two establishes the exact cost optimum for these assignments.
- The donor counterexample has two five-unit obligations and two donors with five units each. One donor can serve both receivers and the other can serve only the first. Moving the first obligation to the restricted donor gives the ten-unit optimum. Phase 2 serves only five units with the committed supplier choices, a 50% gap on delivered quantity.

## Additional complexity

Let `M` be the number of obligations, `S` ships, `J` scheduled legs, `I` inventory accounts, and `K` the greatest combined event count in the inventory accounts touched by a move. Let `B` be the trial budget, `A` the per-query arc cap, and `L` the per-vertex label cap. Setup sorts and indexes obligations and ships in `O(M log M + S log S)`, builds the ledger with up to two time-index insertions per leg, and copies the retained paths. The predecessor scan can inspect up to `O(M)` IDs per selected target. Indexed ship lookup is logarithmic plus a constant number of candidates.

Each trial checks at most three new legs. With the fixed route and departure limits, its route work is `O(A(log A + L))` using the heap and bounded dominance checks; copying, replaying and validating touched timelines costs `O(K)` per bounded set of departure checks. The additional trial work is bounded by `O(B(A(log A + L) + K))`, plus indexing and final schedule ordering. Additional storage is linear in the obligations, ships, accounts, scheduled legs and retained route lengths, plus `O(K + A)` temporary search space. A complete simulation is performed once for the final plan, outside the local-search loop.
