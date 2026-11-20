#include "TestSupport.h"
#include "../simulation/SimulationEngine.h"

namespace regression {
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
