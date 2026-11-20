#include "Demonstration.h"
#include <chrono>
#include <iostream>
#include <vector>
#include "../algorithms/Dijkstra.h"
#include "../algorithms/InfrastructureOptimizer.h"
#include "../algorithms/LogisticsPlanner.h"
#include "../simulation/SimulationEngine.h"

using namespace std;

void demonstrateRouting() {
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
        route.travelTime = route.distance;
        route.fuelConsumption = route.distance;
        route.maxCapacity = 1000;
        route.reliability = 1;
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
    a.travelTime = a.distance;
    a.fuelConsumption = a.distance;
    a.maxCapacity = 1000;
    a.reliability = 1;
    a.fuelConsumption = 30;
    a.isAvailable = true;
    testGalaxy.addRoute(a);

    Route b{};
    b.id = 102;
    b.from = 2;
    b.to = 3;
    b.distance = 5;
    b.travelTime = b.distance;
    b.fuelConsumption = b.distance;
    b.maxCapacity = 1000;
    b.reliability = 1;
    b.fuelConsumption = 30;
    b.isAvailable = true;
    testGalaxy.addRoute(b);

    Route c{};
    c.id = 103;
    c.from = 1;
    c.to = 3;
    c.distance = 20;
    c.travelTime = c.distance;
    c.fuelConsumption = c.distance;
    c.maxCapacity = 1000;
    c.reliability = 1;
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
}
