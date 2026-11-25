#pragma once

#include <vector>
#include <functional>
#include "../graph/GalaxyGraph.h"
#include "Dijkstra.h"
#include "../models/Resource.h"

struct ResourceDemand {
    int planetId;
    double available;
    double required;
    int priority;
    ResourceType type = ResourceType::Food;
    double production = 0;
    double consumption = 0;
    double storage = 1e30;
};

struct ResourceTransfer {
    int fromPlanet;
    int toPlanet;
    double amount;
    double transportationCost = 0.0;
    std::vector<int> path;
    std::vector<int> routeIds;
};

struct AllocationResult {
    std::vector<ResourceTransfer> transfers;
    double totalDelivered = 0.0;
    double remainingShortage = 0.0;
    double totalTransportationCost = 0.0;
    long long nodesExplored = 0;
    long long candidates = 0;
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
        const RouteWeights& weights = {},
        const std::function<void(size_t)>& progress = {},
        bool benefitAware = false
    );
};
