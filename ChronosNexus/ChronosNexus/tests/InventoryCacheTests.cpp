#include "TestSupport.h"
#include "../algorithms/InventoryReservations.h"
#include "../simulation/SimulationEngine.h"

namespace regression {
void testInventoryAndCache() {
    auto [graph, first, second, ship] = makeFlightFixture();
    second.deadline = 6;
    second.priority = first.priority + 1;
    const std::vector<ResourceDemand> chainBalances{{1, 6, 0, 1}, {2, 0, 0, 1}, {3, 0, 0, 1}};
    const std::vector<Mission> noAid;
    const InventoryPlanningInput chainPlanning{chainBalances, chainBalances, noAid};
    const auto chainPlan = MissionScheduler::schedule({first, second}, {ship}, graph,
        {}, false, {}, &chainPlanning);
    const auto chainRun = SimulationEngine::run(graph, chainBalances, {first, second},
        {ship}, chainPlan, {});
    // Two obligations is the upper bound. The only feasible order is 1 then 2.
    require(chainRun.completed == 2 && chainRun.inventoryFailures == 0 &&
        chainPlan.recoveredMissionIds == std::vector<int>{second.id},
        "The second pass must recover the chain using the first delivery as inventory.");

    auto competing = first;
    competing.id = 9;
    auto secondShip = ship;
    secondShip.id = 2;
    const auto competingPlan = MissionScheduler::schedule({first, competing}, {ship, secondShip},
        graph, {}, false, {}, &chainPlanning);
    const auto competingRun = SimulationEngine::run(graph, chainBalances, {first, competing},
        {ship, secondShip}, competingPlan, {});
    // A common source has six units and each obligation requires six, so the exact optimum is one.
    require(competingPlan.rejectedMissionIds.size() == 1 && competingRun.completed == 1 &&
        competingRun.inventoryFailures == 0, "Concurrent withdrawals cannot reserve the same cargo.");

    const std::vector<ResourceDemand> productionBalances{
        {1, 0, 0, 1, ResourceType::Food, 5, 2, 20}, {2, 0, 0, 1}, {3, 0, 0, 1}};
    const InventoryPlanningInput productionPlanning{productionBalances, productionBalances, noAid};
    first.deadline = 3;
    const auto delayed = MissionScheduler::schedule({first}, {ship}, graph,
        {}, false, {}, &productionPlanning);
    require(delayed.scheduled.size() == 1 && delayed.scheduled[0].departureTime == 2 &&
        SimulationEngine::run(graph, productionBalances, {first}, {ship}, delayed, {}).completed == 1,
        "Net production must enable a departure exactly at the latest feasible time.");

    InventoryReservations book(chainBalances);
    require(book.reserve(1, 2, ResourceType::Food, 2, 3, 6, true) &&
        !book.reserve(1, 3, ResourceType::Food, 0, 1, 6, true) &&
        book.reserve(2, 3, ResourceType::Food, 3, 4, 6, false),
        "Rejecting an inserted withdrawal must preserve existing reservations and incoming stock.");
    InventoryReservations boundedSink({{1, 12, 0, 1}, {2, 0, 0, 1, ResourceType::Food, 0, 0, 6}});
    require(boundedSink.reserve(1, 2, ResourceType::Food, 3, 4, 6, true) &&
        !boundedSink.reserve(1, 2, ResourceType::Food, 1, 2, 6, true),
        "An earlier insertion cannot consume storage needed by a reserved arrival.");

    auto alternativeAid = first;
    alternativeAid.origin = 2;
    const std::vector<Mission> eligibleAid{alternativeAid};
    const InventoryPlanningInput supplierPlanning{chainBalances, chainBalances, eligibleAid};
    const auto supplierPlan = MissionScheduler::schedule(eligibleAid, {ship}, graph,
        {}, false, {}, &supplierPlanning);
    require(supplierPlan.revisedOrigins.at(alternativeAid.id) == 1 &&
        supplierPlan.recoveredMissionIds.size() == 1 && supplierPlan.missionsScheduled == 1,
        "A rejected aid obligation can retain its identity while changing supplier.");

    auto changedGraph = graph;
    std::vector<Mission> repeated(4, first);
    std::vector<Spacecraft> fleet(4, ship);
    for (size_t i = 0; i < repeated.size(); ++i) {
        repeated[i].id = static_cast<int>(i + 1);
        repeated[i].deadline = 8;
        fleet[i].id = static_cast<int>(i + 1);
    }
    const std::vector<ResourceDemand> stocked{{1, 60, 0, 1}, {2, 0, 0, 1}};
    const InventoryPlanningInput stockedPlan{stocked, stocked, noAid};
    const auto afterUpdates = MissionScheduler::schedule(repeated, fleet, changedGraph,
        {1, 0, 0, 10}, false, [&](size_t i) {
            if (i == 1) {
                changedGraph.blockRoute(1);
                changedGraph.blockRoute(2);
            } else if (i == 2) {
                changedGraph.unblockRoute(1);
                changedGraph.unblockRoute(2);
            } else if (i == 3) {
                changedGraph.updateRouteRisk(1, 1);
            } else if (i == 4) {
                Route shortcut = *changedGraph.findRoute(2);
                shortcut.id = 77;
                shortcut.distance = shortcut.travelTime = 0.5;
                changedGraph.addRoute(shortcut);
            }
        }, &stockedPlan);
    require(afterUpdates.routeCacheInvalidations == 4 &&
        afterUpdates.scheduled.size() == 4 &&
        afterUpdates.scheduled[0].routeIds == std::vector<int>{1} &&
        afterUpdates.scheduled[1].missionId == 3 &&
        afterUpdates.scheduled[1].routeIds == std::vector<int>{1} &&
        afterUpdates.scheduled[2].routeIds == std::vector<int>{2} &&
        afterUpdates.scheduled[3].routeIds == std::vector<int>{77} &&
        afterUpdates.recoveredMissionIds == std::vector<int>{2},
        "Closure, reopening, risk changes, and new edges must invalidate cached searches.");

    const InventoryPlanningInput uncachedChain{chainBalances, chainBalances, noAid, false};
    first.deadline = 8;
    requireSamePlan(MissionScheduler::schedule({first, second}, {ship}, graph, {}, false, {}, &uncachedChain),
        chainPlan);
    requireSamePlan(MissionScheduler::schedule({first, competing}, {ship, secondShip}, graph,
        {}, false, {}, &uncachedChain), competingPlan);

    Event shock;
    shock.eventType = EventType::ResourceProductionDecrease;
    shock.affectedObjectType = AffectedObjectType::Planet;
    shock.affectedObjectId = 1;
    shock.eventTime = 2;
    shock.severity = 0.5;
    const InventoryPlanningInput uncachedProduction{productionBalances, productionBalances, noAid, false};
    first.deadline = 3;
    requireSamePlan(MissionScheduler::schedule({first}, {ship}, graph, {}, false, {}, &uncachedProduction),
        delayed);
    auto surpriseCut = shock;
    surpriseCut.eventTime = 1;
    surpriseCut.severity = 1;
    require(SimulationEngine::run(graph, productionBalances, {first}, {ship}, delayed,
        {surpriseCut}).inventoryFailures == 1,
        "A production shock can still invalidate a nominal reservation.");
}
}
