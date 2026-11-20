#include "GalaxyGraph.h"

#include <cmath>
#include <atomic>
#include <stdexcept>

void GalaxyGraph::markChanged() {
    // Copies retain the revision of their contents. Diverging copies get distinct revisions.
    static std::atomic<std::uint64_t> nextRevision{0};
    routeRevision = nextRevision.fetch_add(1, std::memory_order_relaxed) + 1;
}

void GalaxyGraph::addLocation(int locationId) {
    if (adjacencyList.try_emplace(locationId).second)
        markChanged();
}

void GalaxyGraph::addRoute(const Route& route) {
    if (routeIndex.contains(route.id))
        throw std::invalid_argument("Duplicate route ID.");
    addLocation(route.from);
    addLocation(route.to);
    auto& routes = adjacencyList.at(route.from);
    routeIndex.emplace(route.id, std::make_pair(route.from, routes.size()));
    routes.push_back(route);
    markChanged();
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
    auto& route = adjacencyList.at(index->second.first)[index->second.second];
    if (route.isAvailable) {
        route.isAvailable = false;
        markChanged();
    }
    return true;
}

bool GalaxyGraph::unblockRoute(int routeId) {
    auto index = routeIndex.find(routeId);
    if (index == routeIndex.end())
        return false;
    auto& route = adjacencyList.at(index->second.first)[index->second.second];
    if (!route.isAvailable) {
        route.isAvailable = true;
        markChanged();
    }
    return true;
}

bool GalaxyGraph::updateRouteRisk(int routeId, double newRisk) {
    auto index = routeIndex.find(routeId);
    if (index == routeIndex.end() || !std::isfinite(newRisk) || newRisk < 0 || newRisk > 1)
        return false;
    auto& route = adjacencyList.at(index->second.first)[index->second.second];
    if (route.risk != newRisk) {
        route.risk = newRisk;
        markChanged();
    }
    return true;
}
