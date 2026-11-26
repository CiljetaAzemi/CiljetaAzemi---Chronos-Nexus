# Civilization objective audit and evaluation

The original specification's Civilization Objective section distinguishes civilization outcomes from the possible final competition formula. This implementation includes an opt-in expanded weighted score and retains the previous score for direct comparisons. The model and coefficients are experimental.

## Previous objective

Write `p(x,r) = x/(x + max(1,r))`. Let `E` be total ending shortage, `U` unserved consumption, `T` the sum of stock targets, and `D` the sum of consumption rates multiplied by the scenario horizon. A subscript `c` restricts sums to food and medicine at colony locations. Let `C` be external cargo requested, `N` the number of external missions, `B` the construction budget, and `I` aggregate positive strategic value of all listed projects.

| Term | Exact legacy definition |
|---|---|
| Resource security | `1 - (p(E,T) + p(U,D))/2` |
| Critical security | `1 - (p(Ec,Tc) + p(Uc,Dc))/2`, or the general security term if there are no colonies |
| Service | Timely accepted cargo clipped to each requested amount and weighted by `max(0,priority)`, divided by the sum of identically weighted requests. Zero if the denominator is zero. |
| Infrastructure | Selected strategic value divided by `I`, or zero when `I = 0`. |
| Operating penalty | Mean of `p(transport credits,C)`, `p(fuel units,C)`, and `p(construction credits,B)`. |
| Waste penalty | `p(waste,C+D)`. |
| Risk penalty | `p(additive route risk,N)`. |

The formula is the weighted sum of the first four terms minus the weighted sum of the last three. Balanced weights are `30,25,20,5,10,5,5`, security weights are `35,35,10,5,5,5,5`, and service weights are `20,15,40,5,10,5,5`. Custom nonnegative weights are normalized to sum to 100. Given valid finite outcomes and normal construction accounting, the loose balanced bound is `[-20,80]` (security `[-15,85]`, service `[-20,80]`). Finite feasible outcomes need not reach these endpoints.

A failed or omitted mission gets zero timely credit. A delayed delivery earns credit only if it still arrives by its deadline. A timely partial delivery earns proportional credit. The legacy formula treats priority zero as zero-weight service. The nominal objective includes initial external obligations, while realized scoring also includes external urgent requests activated through the horizon, whether served or unserved. Generated aid and preventive trips affect stock and operating outcomes. Their cargo does not enter either model's external service reference. Candidate replay uses known production effects. Realized simulation adds the actual event stream. JSON's exported nominal forecast also includes its preventive supply pass, so it can differ from the nominal score used to select the earlier candidate.

## Coverage against the specification

| Objective | Legacy coverage | Expanded coverage and evidence still needed |
|---|---|---|
| Population survival | Indirect through critical colony stock | Population-weighted modeled coverage of essential consumption. Actual survival requires demographic or health data and a validated consequence model. |
| Resource security | Direct ending shortage and unserved consumption, with critical amounts also included in general security | Normalized per-account ending and unserved fractions. Account priority supplies relative importance. Population-covered consumption enters coverage only. |
| Economic growth | Missing as a separate outcome. Production affects stock. | Optional incremental economic development appraisal. Stock effects still come from the simulator. Real growth needs output quantities, valuations, and an economic baseline. |
| Scientific progress | Missing. Location technology is a static input. | Optional research appraisal assigned to an eligible completed project. No technology progression rate is inferred. Observed progress requires milestones or simulated technology changes. |
| Territorial expansion | Missing. Existing colony locations are inputs. | Optional incremental colony-development appraisal. The project must be selected and finish within the horizon. No discoveries, new territory, or acquired area are inferred. |
| Mission success | Direct timely priority-weighted fulfillment | Direct timely fulfillment with positive minimum weight for each external obligation. |
| Transportation cost | Measured route distance times ship operating cost, combined with other penalties | Explicit normalized transportation penalty using the same physical accounting. |
| Resource waste | Direct simulated waste | Explicit normalized waste using fixed reference cargo and consumption. |
| Mission failures | Indirect through lost timely fulfillment | Explicit external service loss `1 - service`, counted once. Raw failure counts remain separate. |
| Risk | Direct additive route-risk exposure | Explicit normalized exposure. Reliability and uncertainty also affect feasibility and route choice. This exposure is not a probability. |
| Energy consumption | Fuel proxy inside operating penalty | Separate fuel penalty. An energy measure requires fuel conversion or direct per-route energy data. |
| Computational cost | External benchmark timing and memory | Still external. Including runtime in decisions would change the meaning of otherwise identical plans and could double-count competition penalties. |

## Expanded mathematical formulation

Each utility is on `[0,1]`. At resource account `a`, divide ending shortage by its positive stock target, and unmet consumption by its positive horizon demand. Clip each loss to one. General security is one minus the weighted mean of the applicable account losses with weight `1 + log(1 + max(priority,0))`. A target of zero contributes no ending term. Zero consumption contributes no consumption term. Critical consumption with population coverage is taken out of this mean so it is counted only in the coverage term.

For each populated location with modeled food or medicine consumption, divide its population equally across the applicable essential accounts. Coverage is one minus the mean unmet essential fraction using these population weights. The proxy measures whether essential consumption is served at populated locations. It estimates neither lives saved nor mortality. Locations without positive essential consumption do not inflate coverage.

The three development utilities divide the sum of selected, eligible project appraisals by the sum of the corresponding appraisal over the fixed eligible project catalog. Eligibility requires a proposed project affordable individually and scheduled to complete within the horizon. JSON accepts nonnegative `economic_appraisal`, `research_appraisal`, and `colony_appraisal`, defaulting to zero. These are scenario-provided estimates of incremental benefits distinct from inventory effects. A scenario author must avoid duplicating the same benefit in multiple appraisals. Appraisals use no assumed growth rate. Selected project IDs are checked against the eligible catalog and combined budget. Without a declared appraisal the component is unavailable.

Service uses the legacy timely cargo ratio with importance `max(1,priority)` per unit. Mission loss is the complementary fraction. Actual cost, fuel, risk, waste and construction spending use the same rational loss function `p` with references `C, C, N, C+D, B`, respectively. Fuel stands for the modeled fuel burden, with no conversion to electricity. The utility for each cost is one minus the loss. Waste and construction spending split one weight equally. An unnecessary trip cannot increase service on its own, and an unused construction project with no declared incremental benefit earns no development utility. A trip may improve both obligation fulfillment and resource security because contract service and stock availability are separate outcomes.

| Utility | Balanced | Security | Service |
|---|---:|---:|---:|
| Essential coverage proxy | 20 | 25 | 15 |
| Resource security | 25 | 35 | 15 |
| External timely fulfillment | 25 | 10 | 40 |
| Economic appraisal | 4 | 4 | 4 |
| Research appraisal | 4 | 4 | 4 |
| Colony appraisal | 4 | 4 | 4 |
| Transportation | 5 | 5 | 5 |
| Fuel | 5 | 5 | 5 |
| Waste and construction spending | 4 | 4 | 4 |
| Route risk | 4 | 4 | 4 |

The balanced profile emphasizes measurable security and service. Coverage receives less than either to reflect proxy uncertainty. Each declared development appraisal receives a small equal weight because the simulator has no observed development trajectory. Transportation and fuel have equal weights for distinct monetary and physical burdens. Waste and budget share a smaller weight, and risk receives a separate weight. The security and service profiles move weight among the three main benefits and keep the cost preferences fixed. These are policy choices for sensitivity testing, with no statistical calibration.

Expanded score is `100 * sum(w_i * utility_i) / sum(active w_i)`, bounded by zero and 100. Coverage, security, service and appraisals only participate when their fixed references are positive. Cost and risk utilities always participate, with reference floors of one. Unavailable benefits do not earn free points. Scaling requested cargo and project appraisals before summing preserves their ratios while avoiding overflow in large finite inputs. Identical algorithms are compared on the same input, active components and weights. Cross-scenario scores can mix different active components and reference scales. Compare raw outcomes alongside paired scores.

## Integration and performance

Phase 3 compares the existing feasible replay candidates under the selected objective. Expanded mode proposes an additional appraised construction portfolio if its selection differs, and uses appraisals instead of generic strategic value in the deficit-targeted construction candidate. Indexed feasibility-aware allocation accepts an augmented schedule only after feasibility replay and improvement under the same objective. Urgent displacement uses the same model for keep/replace, with obligations known at event time and common projected contributions. Baseline allocation, per-mission ordering, spacecraft and route searches, and the preventive planner remain heuristic proposal mechanisms. The objective selects among tested plans. It does not solve the complete planning problem optimally.

With `A` accounts, `M` external obligations, `P` project inputs, and `K` selected projects, expanded setup takes expected `O(A+M+P)` time and space. An evaluation with an empty scenario takes constant time. In general it takes expected `O(A+M+K)` time, with `O(K)` extra space to detect duplicate selections. This assumes ordinary hash-table behavior. Pathological collisions can make hash lookup linear in container size. The bounded candidate set may add one Knapsack call and one replay when appraisals produce a distinct portfolio. The extra Knapsack call costs `O(PB)` time for integral budget `B`, with `O(B+PB)` worst-case storage for the reconstruction records. No all-candidate replay was added to individual routing or assignment checks.

## Competition boundary and reproduction

The professor's possible competition formula combines quality, time, memory and risk or operating cost. The coefficients and exact quality definition are unknown. The internal expanded score already accounts for risk and operating costs. A competition evaluator should use raw outcomes or separate benefit and cost terms so those costs are charged only once. The project exports raw cost, fuel, waste, shortage and risk alongside wall time and peak working set. There is no claimed official competition score.

Set `"objective_model": "expanded"` and optionally `"objective_profile": "security"` or `"service"` in scenario JSON. JSON returns both scores for the same plan. For benchmarks use `--expanded-objective` with Phase 3. [Experimental reproduction](benchmark-results/README.md)

For the scaling and preference interpretation of weighted sums, see Marler and Arora, *The weighted sum method for multi-objective optimization: new insights*. [Primary research article](https://doi.org/10.1007/s00158-009-0460-7)
