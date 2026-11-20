#include "Demonstration.h"
#include <chrono>
#include <iostream>
#include <vector>
#include "../algorithms/Dijkstra.h"
#include "../algorithms/InfrastructureOptimizer.h"
#include "../algorithms/LogisticsPlanner.h"
#include "../simulation/SimulationEngine.h"

using namespace std;

void demonstrateInfrastructureOptimizer() {
    std::vector<Infrastructure> projects(4);

    // Solar Power Station
    projects[0].id = 1;
    projects[0].constructionCost = 40;
    projects[0].strategicValue = 60;

    // Medical Center
    projects[1].id = 2;
    projects[1].constructionCost = 50;
    projects[1].strategicValue = 80;

    // Defense System
    projects[2].id = 3;
    projects[2].constructionCost = 60;
    projects[2].strategicValue = 90;

    // Research Laboratory
    projects[3].id = 4;
    projects[3].constructionCost = 30;
    projects[3].strategicValue = 45;

    auto result = InfrastructureOptimizer::optimize(projects, 100);

    std::cout << "\n=== INFRASTRUCTURE DP TEST ===\n";
    std::cout << "Total cost: " << result.totalCost << "\n";
    std::cout << "Total strategic value: "
        << result.totalStrategicValue << "\n";

    std::cout << "Selected infrastructure IDs: ";
    for (int id : result.selectedInfrastructureIds) {
        std::cout << id << " ";
    }
    std::cout << "\n";
}



void demonstrateCustomMissionScheduler() {
    GalaxyGraph galaxy;
    galaxy.addLocation(1);
    galaxy.addLocation(2);

    Route route{};
    route.id = 900;
    route.from = 1;
    route.to = 2;
    route.distance = 20;
    route.travelTime = route.distance;
    route.fuelConsumption = route.distance;
    route.maxCapacity = 1000;
    route.reliability = 1;
    route.isAvailable = true;
    galaxy.addRoute(route);

    Mission mission{};
    mission.id = 900;
    mission.origin = 1;
    mission.destination = 2;
    mission.cargoAmount = 20;
    mission.deadline = 10;
    mission.priority = 5;

    Spacecraft fast{};
    fast.id = 101;
    fast.currentLocation = 1;
    fast.maxCargoCapacity = 100;
    fast.maxFuelCapacity = 100;
    fast.currentFuel = 100;
    fast.speed = 20;
    fast.availableAt = 0;
    fast.reliability = 1;

    Spacecraft efficient = fast;
    efficient.id = 102;
    efficient.maxCargoCapacity = 25;
    efficient.speed = 10;

    auto result = MissionScheduler::schedule(
        { mission }, { fast, efficient }, galaxy
    );

    std::cout << "\n=== CUSTOM SCHEDULER TEST ===\n";

    if (!result.scheduled.empty()) {
        std::cout << "Selected spacecraft: "
            << result.scheduled[0].spacecraftId << "\n";
        std::cout << "Arrival time: "
            << result.scheduled[0].arrivalTime << "\n";
    }
    else {
        std::cout << "Mission rejected\n";
    }
}

void demonstrateAllocation() {
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
        route.travelTime = route.distance;
        route.fuelConsumption = route.distance;
        route.maxCapacity = 1000;
        route.reliability = 1;
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
}

void demonstrateScheduling() {
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
            route.travelTime = route.distance;
            route.fuelConsumption = route.distance;
            route.maxCapacity = 1000;
            route.reliability = 1;
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
}
