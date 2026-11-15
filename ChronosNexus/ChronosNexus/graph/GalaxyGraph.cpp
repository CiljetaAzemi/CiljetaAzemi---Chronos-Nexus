#include "GalaxyGraph.h"


void GalaxyGraph::addLocation(int locationId) {
    adjacencyList.try_emplace(locationId);
}


void GalaxyGraph::addRoute(const Route& route) {
    // Make sure both locations exist
    addLocation(route.from);
    addLocation(route.to);

    // Add the route to the starting location
    adjacencyList[route.from].push_back(route);
}


const std::vector<Route>& GalaxyGraph::getRoutes(int locationId) const {
    return adjacencyList.at(locationId);
}


bool GalaxyGraph::hasLocation(int locationId) const {
    return adjacencyList.find(locationId) != adjacencyList.end();
}


bool GalaxyGraph::blockRoute(int routeId) {

    for (auto& pair : adjacencyList) {

        for (Route& route : pair.second) {

            if (route.id == routeId) {
                route.isAvailable = false;
                return true;
            }
        }
    }

    return false;
}


bool GalaxyGraph::unblockRoute(int routeId) {

    for (auto& pair : adjacencyList) {

        for (Route& route : pair.second) {

            if (route.id == routeId) {
                route.isAvailable = true;
                return true;
            }
        }
    }

    return false;
}

bool GalaxyGraph::updateRouteRisk(int routeId, double newRisk) {

    // Risk must be between 0 and 1
    if (newRisk < 0.0 || newRisk > 1.0) {
        return false;
    }

    for (auto& pair : adjacencyList) {

        for (Route& route : pair.second) {

            if (route.id == routeId) {
                route.risk = newRisk;
                return true;
            }
        }
    }

    return false;
}
