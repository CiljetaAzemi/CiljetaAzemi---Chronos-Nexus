#pragma once

#include <unordered_map>
#include <vector>
#include "../models/Route.h"

class GalaxyGraph {
private:
    // Each location ID maps to its outgoing routes
    std::unordered_map<int, std::vector<Route>> adjacencyList;
    //This is an adjacency list, a common way to represent a graph.
    //For large, sparse graphs, an adjacency list is generally
    //much more memory-efficient than an adjacency matrix.
public:
    // Add a location (planet, colony or station)
    void addLocation(int locationId);

    // Add a route between two locations
    void addRoute(const Route& route);

    // Get outgoing routes from a location
    const std::vector<Route>& getRoutes(int locationId) const;

    // Check whether a location exists
    bool hasLocation(int locationId) const;


    bool blockRoute(int routeId);

    bool unblockRoute(int routeId);

    bool updateRouteRisk(int routeId, double newRisk);

};
