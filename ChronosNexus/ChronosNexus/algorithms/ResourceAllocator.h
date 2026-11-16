#pragma once

#include <vector>
#include "../graph/GalaxyGraph.h"
#include "Dijkstra.h"

struct ResourceDemand {
    int planetId;
    double available;
    double required;
    int priority;
};

struct ResourceTransfer {
    int fromPlanet;
    int toPlanet;
    double amount;
    double transportationCost = 0.0;
    std::vector<int> path;
};

struct AllocationResult {
    std::vector<ResourceTransfer> transfers;
    double totalDelivered = 0.0;
    double remainingShortage = 0.0;
    double totalTransportationCost = 0.0;
};

class ResourceAllocator {
public:
    // Original greedy baseline
    static AllocationResult allocate(
        std::vector<ResourceDemand> planets
    );

    // Transportation-aware allocation
    static AllocationResult allocateWithRoutes(
        std::vector<ResourceDemand> planets,
        const GalaxyGraph& galaxy,
        const RouteWeights& weights = {}
    );
};
