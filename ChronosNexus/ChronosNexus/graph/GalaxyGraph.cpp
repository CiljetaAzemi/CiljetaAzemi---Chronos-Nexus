#include "GalaxyGraph.h"

#include <cmath>
#include <stdexcept>

void GalaxyGraph::addLocation(int locationId) {
    adjacencyList.try_emplace(locationId);
}

void GalaxyGraph::addRoute(const Route& route) {
    if (routeIndex.contains(route.id))
        throw std::invalid_argument("Duplicate route ID.");
    addLocation(route.from);
    addLocation(route.to);
    auto& routes = adjacencyList.at(route.from);
    routeIndex.emplace(route.id, std::make_pair(route.from, routes.size()));
    routes.push_back(route);
}

const std::vector<Route>& GalaxyGraph::getRoutes(int locationId) const {
    return adjacencyList.at(locationId);
}

bool GalaxyGraph::hasLocation(int locationId) const {
    return adjacencyList.contains(locationId);
}

const Route* GalaxyGraph::findRoute(int routeId) const {
    auto index = routeIndex.find(routeId);
    if (index == routeIndex.end())
        return nullptr;
    return &adjacencyList.at(index->second.first)[index->second.second];
}

GalaxyGraph GalaxyGraph::reversed() const {
    GalaxyGraph graph;
    graph.adjacencyList.reserve(adjacencyList.size());
    graph.routeIndex.reserve(routeIndex.size());
    for (const auto& [location, routes] : adjacencyList) {
        graph.addLocation(location);
        for (auto edge : routes) {
            std::swap(edge.from, edge.to);
            graph.addRoute(edge);
        }
    }
    return graph;
}

bool GalaxyGraph::blockRoute(int routeId) {
    auto index = routeIndex.find(routeId);
    if (index == routeIndex.end())
        return false;
    adjacencyList.at(index->second.first)[index->second.second].isAvailable = false;
    return true;
}

bool GalaxyGraph::unblockRoute(int routeId) {
    auto index = routeIndex.find(routeId);
    if (index == routeIndex.end())
        return false;
    adjacencyList.at(index->second.first)[index->second.second].isAvailable = true;
    return true;
}

bool GalaxyGraph::updateRouteRisk(int routeId, double newRisk) {
    auto index = routeIndex.find(routeId);
    if (index == routeIndex.end() || !std::isfinite(newRisk) || newRisk < 0 || newRisk > 1)
        return false;
    adjacencyList.at(index->second.first)[index->second.second].risk = newRisk;
    return true;
}
