#pragma once

#include <unordered_map>
#include <vector>
#include <cstdint>
#include "../models/Route.h"

class GalaxyGraph {
private:
    // Each location ID maps to its outgoing routes
    std::unordered_map<int, std::vector<Route>> adjacencyList;
    std::unordered_map<int, std::pair<int, size_t>> routeIndex;
    std::uint64_t routeRevision = 0;
    void markChanged();
    //This is an adjacency list, a common way to represent a graph.
    //For large, sparse graphs, an adjacency list is generally
    //much more memory-efficient than an adjacency matrix.
public:
    const auto& adjacency() const { return adjacencyList; }
    // Add a location (planet, colony or station)
    void addLocation(int locationId);

    // Add a route between two locations
    void addRoute(const Route& route);

    // Get outgoing routes from a location
    const std::vector<Route>& getRoutes(int locationId) const;

    // Check whether a location exists
    bool hasLocation(int locationId) const;
    size_t locationCount() const { return adjacencyList.size(); }
    const Route* findRoute(int routeId) const;
    GalaxyGraph reversed() const;
    std::uint64_t revision() const { return routeRevision; }


    bool blockRoute(int routeId);

    bool unblockRoute(int routeId);

    bool updateRouteRisk(int routeId, double newRisk);

};
