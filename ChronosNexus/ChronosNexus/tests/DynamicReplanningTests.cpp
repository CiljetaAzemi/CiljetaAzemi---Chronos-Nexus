#include "TestSupport.h"
#include "../simulation/SimulationEngine.h"

namespace regression {
void testDynamicReplanning() {
    using Reason = SimulationResult::ReplanningDecision::Reason;
    auto [graph, first, second, ship] = makeFlightFixture();
    auto backup = ship;
    backup.id = 2;
    auto other = ship;
    other.id = 3;
    other.currentLocation = 2;
    const std::vector<ResourceDemand> stock{{1, 12, 0, 1}, {2, 12, 0, 1}, {3, 0, 0, 1}};
    SchedulingResult plan;
    plan.scheduled = {{first.id, ship.id, 2, 3, 2, {1, 2}, {1}, {}},
        {second.id, other.id, 1, 2, 5, {2, 3}, {3}, {}}};

    Event failure;
    failure.id = 40;
    failure.eventTime = 1.5;
    failure.eventType = EventType::SpacecraftMechanicalFailure;
    failure.affectedObjectId = ship.id;
    const auto reassigned = SimulationEngine::run(graph, stock, {first, second}, {ship, backup, other}, plan, {failure});
    require(reassigned.completed == 2 && reassigned.replanningDecisions.size() == 1 &&
        reassigned.deliveries.at(second.id).arrival == 2 &&
        reassigned.replanningDecisions[0].previous->spacecraftId == ship.id &&
        reassigned.replanningDecisions[0].replacement->spacecraftId == backup.id,
        "Repair must replace the disabled ship and preserve the unrelated in-flight mission.");

    backup.currentFuel = 1;
    const auto stranded = SimulationEngine::run(graph, stock, {first, second}, {ship, backup, other}, plan, {failure});
    require(stranded.completed == 1 && stranded.failed == 1 && stranded.replanningDecisions.size() == 1 &&
        stranded.replanningDecisions[0].reason == Reason::UnavailableSpacecraft &&
        !stranded.replanningDecisions[0].replacement, "A replacement must have sufficient fuel.");

    Event closure = failure;
    closure.id = 41;
    closure.eventType = EventType::RouteBlocked;
    closure.affectedObjectId = 1;
    const auto rerouted = SimulationEngine::run(graph, stock, {first, second}, {ship, other}, plan, {closure});
    require(rerouted.completed == 2 && rerouted.replanningDecisions.size() == 1 &&
        rerouted.replanningDecisions[0].previous->routeIds == std::vector<int>{1} &&
        rerouted.replanningDecisions[0].replacement->routeIds == std::vector<int>{2},
        "A blocked edge must invalidate and replace the exact pending route.");
    first.deadline = 4;
    const auto unreachable = SimulationEngine::run(graph, stock, {first, second}, {ship, other}, plan, {closure});
    require(unreachable.completed == 1 && unreachable.failed == 1 && unreachable.replanningDecisions.size() == 1 &&
        !unreachable.replanningDecisions[0].replacement,
        "A detour that misses the deadline cannot recover a closure.");

    auto chainFirst = first;
    chainFirst.deadline = 5;
    auto chainSecond = second;
    chainSecond.deadline = 6;
    const auto chainPlan = MissionScheduler::schedule({chainFirst, chainSecond}, {ship}, graph);
    auto dependentStock = stock;
    dependentStock[1].available = 0;
    closure.eventTime = 0;
    const auto delayed = SimulationEngine::run(graph, dependentStock, {chainFirst, chainSecond}, {ship}, chainPlan, {closure});
    require(delayed.completed == 2 && delayed.replanningDecisions.size() == 2 &&
        delayed.replanningDecisions[1].reason == Reason::DependentAssignment &&
        delayed.replanningDecisions[1].replacement->departureTime == 4 && delayed.inventoryFailures == 0,
        "Rerouting must repair a successor that depends on the delayed ship and delivery.");

    plan.scheduled.resize(1);
    backup.currentFuel = 8;
    Mission urgent = first;
    urgent.id = 7;
    urgent.deadline = 5;
    Event request;
    request.id = 42;
    request.eventTime = 1;
    request.eventType = EventType::HighPriorityMission;
    request.affectedObjectId = urgent.id;
    request.urgentMission = urgent;
    const auto accepted = SimulationEngine::run(graph, stock, {first}, {ship, backup}, plan, {request});
    require(accepted.completed == 2 && accepted.deliveries.at(first.id).arrival == 3 &&
        accepted.replanningDecisions.size() == 1 &&
        !accepted.replanningDecisions[0].previous &&
        accepted.replanningDecisions[0].replacement->spacecraftId == backup.id,
        "An urgent arrival must use spare capacity without moving the fixed booking.");

    auto scarce = stock;
    scarce[0].available = 6;
    const auto declined = SimulationEngine::run(graph, scarce, {first}, {ship, backup}, plan, {request});
    require(declined.completed == 1 && declined.failed == 1 && declined.inventoryFailures == 0 &&
        declined.deliveries.at(first.id).arrival == 3 && !declined.replanningDecisions[0].replacement,
        "An urgent mission cannot spend stock reserved for a pending departure.");

    plan.scheduled[0].departureTime = 0;
    plan.scheduled[0].arrivalTime = 1;
    urgent = second;
    urgent.id = 8;
    request.affectedObjectId = urgent.id;
    request.eventTime = 0.5;
    request.urgentMission = urgent;
    scarce[1].available = 0;
    const auto chained = SimulationEngine::run(graph, scarce, {first}, {ship}, plan, {request});
    require(chained.completed == 2 && chained.replanningDecisions[0].replacement &&
        chained.replanningDecisions[0].replacement->departureTime == 1,
        "An urgent repair can reserve a confirmed in-flight delivery and its arriving ship.");

    closure.eventTime = 1.5;
    const auto completed = SimulationEngine::run(graph, stock, {first}, {ship}, plan, {closure});
    require(completed.completed == 1 && completed.replanningDecisions.empty() &&
        completed.deliveries.at(first.id).arrival == 1, "A completed route must not be replanned.");
}
}
