#include "TestSupport.h"
#include "../algorithms/PlanImprover.h"
#include "../simulation/SimulationEngine.h"
#include <iostream>
#include <tuple>

namespace regression {
void testPlanImprovement() {
    auto [graph, first, second, ship] = makeFlightFixture();
    second.deadline = 6;
    second.priority = first.priority + 1;
    const std::vector<ResourceDemand> chainBalances{{1, 6, 0, 1}, {2, 0, 0, 1}, {3, 0, 0, 1}};
    const std::vector<Mission> noAid;
    const InventoryPlanningInput chainPlanning{chainBalances, chainBalances, noAid};
    auto secondShip = ship;
    secondShip.id = 2;

    addTestRoute(graph, 5, 3, 4, 1, 1, 20);
    auto third = second;
    third.id = 3;
    third.origin = 3;
    third.destination = 4;
    third.priority = second.priority + 1;
    auto longChainBalances = chainBalances;
    longChainBalances.push_back({4, 0, 0, 1});
    const InventoryPlanningInput longChainCached{longChainBalances, longChainBalances, noAid};
    const InventoryPlanningInput longChainUncached{longChainBalances, longChainBalances, noAid, false};
    const auto longChain = MissionScheduler::schedule({first, second, third}, {ship}, graph,
        {}, false, {}, &longChainCached);
    requireSamePlan(MissionScheduler::schedule({first, second, third}, {ship}, graph,
        {}, false, {}, &longChainUncached), longChain);
    require(longChain.missionsScheduled == 2, "Cache changed the bounded three-leg retry outcome.");
    const auto improvedChain = PlanImprover::improve(longChain, {first, second, third}, {ship},
        graph, longChainBalances, {}, 30);
    const auto chainActual = SimulationEngine::run(graph, longChainBalances, {first, second, third},
        {ship}, improvedChain, {});
    require(improvedChain.locallyRecovered == 1 && chainActual.completed == 3 &&
        chainActual.inventoryFailures == 0, "Local search must attain the three-obligation chain optimum.");

    GalaxyGraph constrained;
    for (const auto& [id, fuel, time] : std::vector<std::tuple<int, double, double>>{
        {1, 1, 12}, {2, 12, 1}, {3, 6, 6}}) {
        Route route{};
        route.id = id;
        route.from = 1;
        route.to = 2;
        route.distance = id == 2 ? 1 : 2;
        route.travelTime = time;
        route.fuelConsumption = fuel;
        route.reliability = 1;
        route.maxCapacity = 6;
        route.isAvailable = true;
        constrained.addRoute(route);
    }
    auto tight = first;
    tight.deadline = 6;
    auto lowFuel = ship;
    lowFuel.currentFuel = 6;
    const std::vector<Mission> tightJobs{tight};
    const auto missed = MissionScheduler::schedule(tightJobs, {lowFuel}, constrained, {}, false, {}, &chainPlanning);
    const auto found = PlanImprover::improve(missed, tightJobs, {lowFuel}, constrained, chainBalances, {}, 30);
    require(missed.missionsScheduled == 0 && found.missionsScheduled == 1 &&
        found.scheduled.front().routeIds == std::vector<int>{3} &&
        SimulationEngine::run(constrained, chainBalances, tightJobs, {lowFuel}, found, {}).completed == 1,
        "Only the middle route meets both six-unit bounds.");

    const std::vector<ResourceDemand> donorTarget{{1, 6, 6, 1}, {2, 0, 0, 1}};
    const auto guarded = PlanImprover::improve(missed, tightJobs, {lowFuel}, constrained, donorTarget, {}, 30);
    require(guarded.missionsScheduled == 0, "An improvement cannot drain protected ending stock.");

    auto costly = ship;
    costly.operatingCost = 100;
    auto cheap = secondShip;
    cheap.operatingCost = 1;
    cheap.availableAt = 1;
    const auto incumbent = MissionScheduler::schedule({first}, {costly, cheap}, graph, {},
        true, {}, &chainPlanning);
    const auto relocated = PlanImprover::improve(incumbent, {first}, {costly, cheap}, graph,
        chainBalances, {}, 30);
    require(incumbent.scheduled.front().spacecraftId == costly.id &&
        relocated.scheduled.front().spacecraftId == cheap.id,
        "Equivalent timely service can be relocated to a cheaper ship.");
    auto simpleGraph = [](const std::vector<std::pair<int, int>>& links) {
        GalaxyGraph network;
        int id = 0;
        for (const auto& [from, to] : links) {
            Route route{};
            route.id = ++id;
            route.from = from;
            route.to = to;
            route.distance = route.travelTime = route.fuelConsumption = 1;
            route.reliability = 1;
            route.isAvailable = true;
            route.maxCapacity = 10;
            network.addRoute(route);
        }
        return network;
    };
    const auto loop = simpleGraph({{1, 2}, {2, 1}, {1, 3}});
    auto last = first;
    last.destination = 3;
    last.priority = 9;
    auto outbound = first;
    outbound.id = 2;
    outbound.priority = 2;
    auto inbound = outbound;
    inbound.id = 3;
    inbound.origin = 2;
    inbound.destination = 1;
    inbound.deadline = 8;
    const std::vector<Mission> resequence{last, outbound, inbound};
    const auto beforeLoop = MissionScheduler::schedule(resequence, {ship}, loop, {}, false, {}, &chainPlanning);
    const auto afterLoop = PlanImprover::improve(beforeLoop, resequence, {ship}, loop, chainBalances, {}, 30);
    require(beforeLoop.missionsScheduled == 1 && afterLoop.missionsScheduled == 3 &&
        SimulationEngine::run(loop, chainBalances, resequence, {ship}, afterLoop, {}).completed == 3,
        "A returning enabling pair can be inserted before the previous terminal leg.");

    auto exchangeGraph = simpleGraph({{1, 2}});
    Route longRoute = *exchangeGraph.findRoute(1);
    longRoute.id = 2;
    longRoute.to = 3;
    longRoute.distance = longRoute.travelTime = longRoute.fuelConsumption = 5;
    exchangeGraph.addRoute(longRoute);
    const std::vector<ResourceDemand> exchangeStock{{1, 12, 0, 1}, {2, 0, 0, 1}, {3, 0, 0, 1}};
    const InventoryPlanningInput exchangeInput{exchangeStock, exchangeStock, noAid};
    const std::vector<Mission> exchangeJobs{last, outbound};
    const auto beforeExchange = MissionScheduler::schedule(exchangeJobs, {costly, cheap}, exchangeGraph,
        {}, true, {}, &exchangeInput);
    const auto afterExchange = PlanImprover::improve(beforeExchange, exchangeJobs, {costly, cheap},
        exchangeGraph, exchangeStock, {}, 30);
    const auto exchanged = SimulationEngine::run(exchangeGraph, exchangeStock, exchangeJobs,
        {costly, cheap}, afterExchange, {});
    require(beforeExchange.scheduled.size() == 2 &&
        beforeExchange.scheduled.front().spacecraftId == costly.id && exchanged.completed == 2 &&
        exchanged.transportationCost == 105,
        "Swapping terminal assignments must realize the cheaper of the two feasible ship pairings.");

    const auto supplyGraph = simpleGraph({{1, 3}, {1, 4}, {2, 3}});
    auto x = first;
    x.destination = 3;
    x.cargoAmount = 5;
    x.priority = 2;
    auto y = x;
    y.id = 2;
    y.destination = 4;
    y.priority = 1;
    auto supplierB = secondShip;
    supplierB.currentLocation = 2;
    const std::vector<ResourceDemand> twoDonors{{1, 5, 0, 1}, {2, 5, 0, 1}, {3, 0, 5, 2}, {4, 0, 5, 1}};
    const std::vector<Mission> competingAid{x, y};
    const InventoryPlanningInput donorsPlan{twoDonors, twoDonors, competingAid};
    const auto greedyDonor = MissionScheduler::schedule(competingAid, {ship, supplierB}, supplyGraph,
        {}, false, {}, &donorsPlan);
    const auto localDonor = PlanImprover::improve(greedyDonor, competingAid, {ship, supplierB},
        supplyGraph, twoDonors, {}, 30);
    x.origin = 2;
    const auto donorWitness = MissionScheduler::schedule({x, y}, {ship, supplierB}, supplyGraph,
        {}, false, {}, &donorsPlan);
    require(localDonor.missionsScheduled == 1 && donorWitness.missionsScheduled == 2 &&
        SimulationEngine::run(supplyGraph, twoDonors, {x, y}, {ship, supplierB}, donorWitness, {}).deliveredAmount == 10,
        "Changing a committed aid supplier remains outside the local neighborhood.");
    std::cout << "Phase 2 exact fixtures: chain 3/3, constrained route 1/1, "
                 "scarcity guard 0/0, cheaper relocation and swap, terminal resequence 3/3, "
                 "donor counterexample 1/2.\n";
}
}
