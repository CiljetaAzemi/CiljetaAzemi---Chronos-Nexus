#include "TestSupport.h"
#include "../simulation/SimulationEngine.h"
#include <cmath>

namespace regression {
void testRouteUncertainty() {
    auto route = [](int id, double time, double risk, double reliability) {
        Route edge{};
        edge.id = id;
        edge.from = 1;
        edge.to = 2;
        edge.distance = 4;
        edge.travelTime = time;
        edge.fuelConsumption = 1;
        edge.maxCapacity = 20;
        edge.risk = risk;
        edge.reliability = reliability;
        edge.isAvailable = true;
        return edge;
    };
    auto variable = route(1, 2, 0.1, 0.75);
    variable.minimumTravelTime = 1;
    variable.maximumTravelTime = 6;
    GalaxyGraph choices;
    choices.addRoute(variable);
    choices.addRoute(route(2, 3, 0.4, 0.95));
    choices.addRoute(route(3, 0, 0, 0));
    require(Dijkstra::findShortestPath(choices, 1, 2, {0, 0, 1}).routeIds == std::vector<int>{2} &&
        Dijkstra::findShortestPath(choices, 1, 2, {0, 0, 0, 0, 1}).routeIds == std::vector<int>{2} &&
        Dijkstra::findShortestPath(choices, 1, 2, {0, 0, 0, 1}).routeIds == std::vector<int>{1},
        "Travel-time bounds and reliability must affect route choice separately from risk.");
    GalaxyGraph multiLeg;
    multiLeg.addRoute(variable);
    auto next = route(4, 3, 0.2, 0.8);
    next.from = 2;
    next.to = 3;
    multiLeg.addRoute(next);
    const auto path = Dijkstra::findShortestPath(multiLeg, 1, 3);
    require(path.found && path.metrics.minimumTime == 4 && path.metrics.expectedTime == 5 &&
        path.metrics.time == 9 && std::abs(path.metrics.reliability - 0.6) < 1e-12 &&
        std::abs(path.metrics.risk - 0.3) < 1e-12,
        "Path time bounds and risk must add, and independent edge reliabilities must multiply.");
    auto connector = route(5, 0, 0, 1);
    connector.from = 2;
    connector.to = 4;
    multiLeg.addRoute(connector);
    connector.id = 6;
    std::swap(connector.from, connector.to);
    multiLeg.addRoute(connector);
    auto blocked = route(7, 0.5, 0, 1);
    blocked.to = 3;
    blocked.isAvailable = false;
    multiLeg.addRoute(blocked);
    const auto bounds = Dijkstra::reverseTimeBounds(multiLeg.reversed(), 3);
    require(bounds.time.at(1) <= 9 && bounds.time.at(1) > 8.99 &&
        bounds.time.at(4) <= 3 && bounds.time.at(4) > 2.99 &&
        !Dijkstra::reverseTimeBounds(multiLeg.reversed(), 1).time.contains(3),
        "Reverse timing must use available directed edges, upper times, and tolerate zero-time cycles.");

    auto [unused, mission, other, slow] = makeFlightFixture();
    mission.deadline = 4;
    GalaxyGraph fixed, uncertain;
    fixed.addRoute(route(1, 2, 0.1, 0.75));
    uncertain.addRoute(variable);
    auto fast = slow;
    fast.id = slow.id + 1;
    fast.speed = 2;
    require(MissionScheduler::schedule({mission}, {slow}, fixed).missionsScheduled == 1 &&
        MissionScheduler::schedule({mission}, {slow}, uncertain).missionsScheduled == 0,
        "An expected-time arrival alone cannot make the slower ship feasible.");
    const auto scheduled = MissionScheduler::schedule({mission}, {slow, fast}, uncertain);
    require(scheduled.scheduled.size() == 1 && scheduled.scheduled[0].spacecraftId == fast.id &&
        scheduled.scheduled[0].estimate.expectedArrival == 1 && scheduled.scheduled[0].arrivalTime == 3,
        "The planner must select the ship that can meet the upper-bound deadline.");
    const std::vector<ResourceDemand> stock{{1, 20, 0, 1}, {2, 20, 0, 1}};
    const auto result = SimulationEngine::run(uncertain, stock, {mission}, {slow, fast}, scheduled, {});
    require(result.completed == 1 && result.deliveries.at(mission.id).arrival == 3 &&
        result.deliveries.at(mission.id).estimate.earliestArrival == 0.5,
        "Simulation must honor the same conservative timing as scheduling.");
    Event closure;
    closure.eventType = EventType::RouteBlocked;
    closure.affectedObjectId = 2;
    const auto initial = MissionScheduler::schedule({mission}, {slow}, choices, {0, 0, 1});
    const auto disrupted = SimulationEngine::run(choices, stock, {mission}, {slow}, initial, {closure});
    require(disrupted.failed == 1 && !disrupted.replanningDecisions.at(0).replacement,
        "Replanning must reject a detour whose upper time exceeds the deadline.");
}

void testRoutingAndSimulation() {
    auto [graph, first, second, ship] = makeFlightFixture();
    const auto schedule = MissionScheduler::schedule({first, second}, {ship}, graph);
    require(schedule.scheduled.size() == 2 && schedule.scheduled[0].routeIds == std::vector<int>{1},
        "The original two missions should be chained on the intended exact routes.");
    require(schedule.scheduled[0].fuelUsed == 2 && schedule.scheduled[1].arrivalTime == 2,
        "Planning must use edge fuel and travel time.");
    const auto capacity = Dijkstra::findShortestPath(graph, 1, 3, {}, 6);
    require(capacity.routeIds == std::vector<int>({1, 3}), "An undersized direct edge must be skipped.");
    require(!Dijkstra::findShortestPath(graph, 1, 3, {-1}).found,
        "Dijkstra must reject negative weights.");
    Event closure;
    closure.eventType = EventType::RouteBlocked;
    closure.affectedObjectId = 1;
    const std::vector<ResourceDemand> inventory{{1, 20, 0, 1}, {2, 20, 0, 1}, {3, 0, 0, 1}};
    const auto disrupted = SimulationEngine::run(graph, inventory, {first, second}, {ship},
        schedule, {closure});
    require(disrupted.completed == 1 && disrupted.failed == 1 && disrupted.rerouted == 1 &&
        disrupted.fuel == 3 && disrupted.distance == 4,
        "Closing a parallel edge must use the detour and prevent overlapping the next trip.");
    second.deadline = 6;
    const auto relaxed = SimulationEngine::run(graph, inventory, {first, second}, {ship},
        schedule, {closure});
    require(relaxed.completed == 2 && relaxed.failed == 0 && relaxed.fuel == 8,
        "A delayed second trip should depart on arrival when still feasible.");
    ship.currentFuel = 1;
    require(MissionScheduler::schedule({first}, {ship}, graph).scheduled.empty(),
        "The scheduler must reject insufficient fuel.");

    Event shock;
    shock.eventType = EventType::ResourceProductionDecrease;
    shock.affectedObjectType = AffectedObjectType::Planet;
    shock.affectedObjectId = 1;
    shock.eventTime = 2;
    shock.severity = 0.5;
    Event construction = shock;
    construction.eventType = EventType::ConstructionCompleted;
    construction.eventTime = 3;
    construction.severity = 2;
    const auto changedStock = SimulationEngine::run(graph,
        {{1, 10, 0, 1, ResourceType::Food, 2, 1, 100}},
        {}, {}, {}, {shock, construction}, {}, false, 5);
    require(changedStock.eventsProcessed == 2 && changedStock.finalResources.at(1) == 16,
        "Production changes must apply only after their event times.");

    first.cargoType = ResourceType::Medicine;
    ship.currentFuel = 8;
    const auto medicineSchedule = MissionScheduler::schedule({first}, {ship}, graph);
    const auto missingMedicine = SimulationEngine::run(graph, inventory, {first}, {ship},
        medicineSchedule, {});
    require(missingMedicine.failed == 1 && missingMedicine.deliveredAmount == 0,
        "Food stock cannot satisfy a medicine mission.");
}
}
