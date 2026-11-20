#include "Demonstration.h"
#include <chrono>
#include <iostream>
#include <vector>
#include "../algorithms/Dijkstra.h"
#include "../algorithms/InfrastructureOptimizer.h"
#include "../algorithms/LogisticsPlanner.h"
#include "../simulation/SimulationEngine.h"

using namespace std;

void demonstrateLogisticsAndDisruptions() {
    cout << "\n=== END-TO-END LOGISTICS TEST ===\n";

    GalaxyGraph logisticsGalaxy;

    logisticsGalaxy.addLocation(1);
    logisticsGalaxy.addLocation(2);
    logisticsGalaxy.addLocation(3);

    Route routeA{};
    routeA.id = 401;
    routeA.from = 1;
    routeA.to = 3;
    routeA.distance = 20;
    routeA.travelTime = routeA.distance;
    routeA.fuelConsumption = routeA.distance;
    routeA.maxCapacity = 1000;
    routeA.reliability = 1;
    routeA.isAvailable = true;
    logisticsGalaxy.addRoute(routeA);

    Route routeB{};
    routeB.id = 402;
    routeB.from = 2;
    routeB.to = 3;
    routeB.distance = 10;
    routeB.travelTime = routeB.distance;
    routeB.fuelConsumption = routeB.distance;
    routeB.maxCapacity = 1000;
    routeB.reliability = 1;
    routeB.isAvailable = true;
    logisticsGalaxy.addRoute(routeB);

    // Planet 3 needs 50 food units.
    // Planet 2 can supply 30; Planet 1 can supply 40.
    vector<ResourceDemand> logisticsDemands = {
        { 1, 60, 20, 1 },
        { 2, 50, 20, 1 },
        { 3, 0, 50, 3 }
    };

    Spacecraft logisticsShip1{};
    logisticsShip1.id = 101;
    logisticsShip1.currentLocation = 2;
    logisticsShip1.maxCargoCapacity = 40;
    logisticsShip1.maxFuelCapacity = 100;
    logisticsShip1.currentFuel = 100;
    logisticsShip1.speed = 10;
    logisticsShip1.availableAt = 0;
    logisticsShip1.reliability = 0.99;

    Spacecraft logisticsShip2{};
    logisticsShip2.id = 102;
    logisticsShip2.currentLocation = 1;
    logisticsShip2.maxCargoCapacity = 50;
    logisticsShip2.maxFuelCapacity = 100;
    logisticsShip2.currentFuel = 100;
    logisticsShip2.speed = 10;
    logisticsShip2.availableAt = 0;
    logisticsShip2.reliability = 0.98;

    vector<Spacecraft> logisticsShips = {
        logisticsShip1, logisticsShip2
    };

    LogisticsPlan plan = LogisticsPlanner::createPlan(
        logisticsDemands,
        logisticsShips,
        logisticsGalaxy,
        ResourceType::Food,
        10.0
    );

    cout << "Planned transfers: "
        << plan.allocation.transfers.size() << endl;

    cout << "Generated missions: "
        << plan.missions.size() << endl;

    cout << "Scheduled missions: "
        << plan.scheduling.missionsScheduled << endl;

    cout << "Rejected missions: "
        << plan.scheduling.rejectedMissionIds.size() << endl;

    for (const auto& mission : plan.scheduling.scheduled) {
        cout << "Mission " << mission.missionId
            << " -> Ship " << mission.spacecraftId
            << " | Arrival: " << mission.arrivalTime
            << endl;
    }




    cout << "\n=== SIMULATION TEST ===\n";

    // First test: all scheduled deliveries succeed.
    vector<Event> noEvents;

    SimulationResult normal = SimulationEngine::run(
        logisticsGalaxy,
        logisticsDemands,
        plan.missions,
        logisticsShips,
        plan.scheduling,
        noEvents
    );

    cout << "Normal completed: "
        << normal.completed << endl;

    cout << "Normal failed: "
        << normal.failed << endl;

    cout << "Normal delivered: "
        << normal.deliveredAmount << endl;

    cout << "Normal shortage: "
        << normal.remainingShortage << endl;

    // Second test: block Planet 2 -> Planet 3
    // before the first spacecraft departs.
    Event blockage{};
    blockage.id = 1;
    blockage.eventTime = 0.0;
    blockage.eventType = EventType::RouteBlocked;
    blockage.affectedObjectType = AffectedObjectType::Route;
    blockage.affectedObjectId = 402;

    vector<Event> disruptions = { blockage };

    SimulationResult disrupted = SimulationEngine::run(
        logisticsGalaxy,
        logisticsDemands,
        plan.missions,
        logisticsShips,
        plan.scheduling,
        disruptions
    );

    cout << "\nAfter route blockage:\n";

    cout << "Completed: "
        << disrupted.completed << endl;

    cout << "Failed: "
        << disrupted.failed << endl;

    cout << "Delivered: "
        << disrupted.deliveredAmount << endl;

    cout << "Remaining shortage: "
        << disrupted.remainingShortage << endl;

    cout << "Events processed: "
        << disrupted.eventsProcessed << endl;




    cout << "\n=== DYNAMIC REPLANNING TESTS ===\n";

    // TEST 1: Alternative route exists.
    GalaxyGraph rerouteGalaxy = logisticsGalaxy;

    Route alternativeA{};
    alternativeA.id = 403;
    alternativeA.from = 2;
    alternativeA.to = 1;
    alternativeA.distance = 5;
    alternativeA.travelTime = alternativeA.distance;
    alternativeA.fuelConsumption = alternativeA.distance;
    alternativeA.maxCapacity = 1000;
    alternativeA.reliability = 1;
    alternativeA.isAvailable = true;
    rerouteGalaxy.addRoute(alternativeA);

    // After route 402 is blocked:
    // Planet 2 -> Planet 1 -> Planet 3
    // Distance = 5 + 20 = 25.

    SimulationResult rerouted = SimulationEngine::run(
        rerouteGalaxy,
        logisticsDemands,
        plan.missions,
        logisticsShips,
        plan.scheduling,
        disruptions
    );

    cout << "\nTEST 1 - Alternative available\n";
    cout << "Completed: " << rerouted.completed << endl;
    cout << "Failed: " << rerouted.failed << endl;
    cout << "Rerouted: " << rerouted.rerouted << endl;
    cout << "Delivered: " << rerouted.deliveredAmount << endl;
    cout << "Shortage: " << rerouted.remainingShortage << endl;

    // TEST 2: No alternative route.
    // Reuse our original graph with blocked route 402.
    cout << "\nTEST 2 - No alternative\n";
    cout << "Completed: " << disrupted.completed << endl;
    cout << "Failed: " << disrupted.failed << endl;
    cout << "Delivered: " << disrupted.deliveredAmount << endl;

    // TEST 3: Alternative route exceeds deadline.
    GalaxyGraph slowGalaxy = logisticsGalaxy;

    Route slowAlternative{};
    slowAlternative.id = 404;
    slowAlternative.from = 2;
    slowAlternative.to = 1;
    slowAlternative.distance = 200;
    slowAlternative.travelTime = slowAlternative.distance;
    slowAlternative.fuelConsumption = slowAlternative.distance;
    slowAlternative.maxCapacity = 1000;
    slowAlternative.reliability = 1;
    slowAlternative.isAvailable = true;
    slowGalaxy.addRoute(slowAlternative);

    SimulationResult tooSlow = SimulationEngine::run(
        slowGalaxy,
        logisticsDemands,
        plan.missions,
        logisticsShips,
        plan.scheduling,
        disruptions
    );

    cout << "\nTEST 3 - Alternative too slow\n";
    cout << "Completed: " << tooSlow.completed << endl;
    cout << "Failed: " << tooSlow.failed << endl;
    cout << "Rerouted: " << tooSlow.rerouted << endl;
    cout << "Delivered: " << tooSlow.deliveredAmount << endl;
}
