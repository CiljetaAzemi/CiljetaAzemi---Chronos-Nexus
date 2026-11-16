
#include <iostream>
#include <chrono>
#include "graph/GalaxyGraph.h"
#include "algorithms/Dijkstra.h"
#include "algorithms/ResourceAllocator.h"
#include "algorithms/MissionScheduler.h"
#include "algorithms/LogisticsPlanner.h"

using namespace std;

int main() {

    GalaxyGraph galaxy;

    // 1. Create locations
    for (int i = 1; i <= 5; i++) {
        galaxy.addLocation(i);
    }

    // 2. Add routes
    auto addRoute = [&](int id, int from, int to, double distance) {
        Route route{};
        route.id = id;
        route.from = from;
        route.to = to;
        route.distance = distance;
        route.isAvailable = true;

        galaxy.addRoute(route);
        };

    addRoute(1, 1, 2, 10);
    addRoute(2, 1, 3, 30);
    addRoute(3, 2, 3, 5);
    addRoute(4, 3, 4, 8);
    addRoute(5, 2, 4, 25);
    addRoute(6, 4, 5, 4);

    // 3. Run Dijkstra
    auto startTime = chrono::steady_clock::now();

    PathResult result =
        Dijkstra::findShortestPath(galaxy, 1, 5);

    auto endTime = chrono::steady_clock::now();

    double elapsed =
        chrono::duration<double, milli>(
            endTime - startTime
        ).count();

    // 4. Display results
    cout << "=== DIJKSTRA TEST ===\n";

    if (result.found) {
        cout << "Shortest path: ";

        for (int node : result.path) {
            cout << node << " ";
        }

        cout << "\nTotal distance: "
            << result.totalCost << endl;
    }
    else {
        cout << "No path found.\n";
    }

    cout << "Nodes explored: "
        << result.nodesExplored << endl;

    cout << "Execution time: "
        << elapsed << " ms\n";

    // 5. Test blocked route
    cout << "\n=== BLOCKED ROUTE TEST ===\n";

    galaxy.blockRoute(3);

    PathResult blocked =
        Dijkstra::findShortestPath(galaxy, 1, 5);

    if (blocked.found) {
        cout << "New path: ";

        for (int node : blocked.path) {
            cout << node << " ";
        }

        cout << "\nNew distance: "
            << blocked.totalCost << endl;
    }
    else {
        cout << "No path found.\n";
    }

    // 6. Test unreachable destination
    cout << "\n=== UNREACHABLE TEST ===\n";

    galaxy.blockRoute(5);
    galaxy.blockRoute(2);

    PathResult unreachable =
        Dijkstra::findShortestPath(galaxy, 1, 5);

    cout << "Path found: "
        << unreachable.found << endl;


    cout << "\n=== MULTIOBJECTIVE ROUTING ===\n";

    // Restore previously blocked routes
    galaxy.unblockRoute(2);
    galaxy.unblockRoute(3);
    galaxy.unblockRoute(5);

    // Prioritize distance
    RouteWeights distanceWeights{ 1.0, 0.0, 0.0, 0.0 };

    // Prioritize fuel
    RouteWeights fuelWeights{ 0.0, 1.0, 0.0, 0.0 };

    PathResult shortest =
        Dijkstra::findShortestPath(galaxy, 1, 5, distanceWeights);

    PathResult fuelEfficient =
        Dijkstra::findShortestPath(galaxy, 1, 5, fuelWeights);

    cout << "Distance optimized cost: "
        << shortest.totalCost << endl;

    cout << "Fuel optimized cost: "
        << fuelEfficient.totalCost << endl;



    cout << "\n=== WEIGHT COMPARISON TEST ===\n";

    GalaxyGraph testGalaxy;

    for (int i = 1; i <= 3; i++) {
        testGalaxy.addLocation(i);
    }

    Route a{};
    a.id = 101;
    a.from = 1;
    a.to = 2;
    a.distance = 5;
    a.fuelConsumption = 30;
    a.isAvailable = true;
    testGalaxy.addRoute(a);

    Route b{};
    b.id = 102;
    b.from = 2;
    b.to = 3;
    b.distance = 5;
    b.fuelConsumption = 30;
    b.isAvailable = true;
    testGalaxy.addRoute(b);

    Route c{};
    c.id = 103;
    c.from = 1;
    c.to = 3;
    c.distance = 20;
    c.fuelConsumption = 10;
    c.isAvailable = true;
    testGalaxy.addRoute(c);

    RouteWeights byDistance{ 1.0, 0.0, 0.0, 0.0 };
    RouteWeights byFuel{ 0.0, 1.0, 0.0, 0.0 };

    auto distanceResult =
        Dijkstra::findShortestPath(testGalaxy, 1, 3, byDistance);

    auto fuelResult =
        Dijkstra::findShortestPath(testGalaxy, 1, 3, byFuel);

    cout << "Distance priority: ";
    for (int node : distanceResult.path)
        cout << node << " ";
    cout << "| Cost: " << distanceResult.totalCost << endl;

    cout << "Fuel priority: ";
    for (int node : fuelResult.path)
        cout << node << " ";
    cout << "| Cost: " << fuelResult.totalCost << endl;




    cout << "\n=== RESOURCE ALLOCATION TEST ===\n";

    vector<ResourceDemand> planets = {
        { 1, 100, 20, 1 },  // Earth
        { 2, 10, 50, 3 },   // Mars
        { 3, 5, 35, 2 }     // Jupiter
    };

    AllocationResult allocation =
        ResourceAllocator::allocate(planets);

    for (const auto& transfer : allocation.transfers) {
        cout << "Planet " << transfer.fromPlanet
            << " -> Planet " << transfer.toPlanet
            << " | Amount: " << transfer.amount
            << endl;
    }

    cout << "Total delivered: "
        << allocation.totalDelivered << endl;

    cout << "Remaining shortage: "
        << allocation.remainingShortage << endl;



    cout << "\n=== TRANSPORTATION-AWARE ALLOCATION ===\n";

    GalaxyGraph supplyGalaxy;

    for (int i = 1; i <= 3; i++)
        supplyGalaxy.addLocation(i);

    auto connect = [&](int id, int from, int to, double distance) {
        Route route{};
        route.id = id;
        route.from = from;
        route.to = to;
        route.distance = distance;
        route.isAvailable = true;
        supplyGalaxy.addRoute(route);
        };

    // Planet 1: far supplier
    connect(201, 1, 3, 100);

    // Planet 2: nearby supplier
    connect(202, 2, 3, 10);

    vector<ResourceDemand> supplyPlanets = {
        { 1, 100, 20, 1 },
        { 2, 60, 20, 1 },
        { 3, 0, 50, 3 }
    };

    AllocationResult optimized =
        ResourceAllocator::allocateWithRoutes(
            supplyPlanets,
            supplyGalaxy
        );

    for (const auto& transfer : optimized.transfers) {
        cout << "Planet " << transfer.fromPlanet
            << " -> Planet " << transfer.toPlanet
            << " | Amount: " << transfer.amount
            << " | Cost: " << transfer.transportationCost
            << endl;
    }

    cout << "Total delivered: "
        << optimized.totalDelivered << endl;

    cout << "Remaining shortage: "
        << optimized.remainingShortage << endl;

    cout << "Transportation cost: "
        << optimized.totalTransportationCost << endl;




    cout << "\n=== MISSION SCHEDULING TEST ===\n";

    GalaxyGraph missionGalaxy;

    for (int i = 1; i <= 3; i++)
        missionGalaxy.addLocation(i);

    auto addMissionRoute = [&](int id, int from,
        int to, double distance) {
            Route route{};
            route.id = id;
            route.from = from;
            route.to = to;
            route.distance = distance;
            route.isAvailable = true;
            missionGalaxy.addRoute(route);
        };

    addMissionRoute(301, 1, 2, 10);
    addMissionRoute(302, 2, 3, 15);

    // Use existing Spacecraft model
    Spacecraft ship1{};
    ship1.id = 1;
    ship1.currentLocation = 1;
    ship1.maxCargoCapacity = 100;
    ship1.maxFuelCapacity = 100;
    ship1.currentFuel = 100;
    ship1.speed = 5;
    ship1.availableAt = 0;

    Spacecraft ship2{};
    ship2.id = 2;
    ship2.currentLocation = 1;
    ship2.maxCargoCapacity = 30;
    ship2.maxFuelCapacity = 50;
    ship2.currentFuel = 50;
    ship2.speed = 10;
    ship2.availableAt = 0;

    vector<Spacecraft> ships = { ship1, ship2 };

    // Use existing Mission model
    Mission mission1{};
    mission1.id = 1;
    mission1.origin = 1;
    mission1.destination = 2;
    mission1.cargoAmount = 50;
    mission1.deadline = 5;
    mission1.priority = 3;

    Mission mission2{};
    mission2.id = 2;
    mission2.origin = 2;
    mission2.destination = 3;
    mission2.cargoAmount = 40;
    mission2.deadline = 10;
    mission2.priority = 2;

    Mission mission3{};
    mission3.id = 3;
    mission3.origin = 1;
    mission3.destination = 3;
    mission3.cargoAmount = 200;
    mission3.deadline = 10;
    mission3.priority = 1;

    vector<Mission> missions = {
        mission1, mission2, mission3
    };

    SchedulingResult schedule =
        MissionScheduler::schedule(
            missions, ships, missionGalaxy
        );

    for (const auto& scheduled : schedule.scheduled) {
        cout << "Mission " << scheduled.missionId
            << " assigned to Ship "
            << scheduled.spacecraftId
            << " | Arrival: "
            << scheduled.arrivalTime
            << " | Fuel: "
            << scheduled.fuelUsed
            << endl;
    }

    cout << "Scheduled missions: "
        << schedule.missionsScheduled << endl;

    cout << "Rejected missions: "
        << schedule.rejectedMissionIds.size() << endl;




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
    routeA.isAvailable = true;
    logisticsGalaxy.addRoute(routeA);

    Route routeB{};
    routeB.id = 402;
    routeB.from = 2;
    routeB.to = 3;
    routeB.distance = 10;
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

    return 0;
}
