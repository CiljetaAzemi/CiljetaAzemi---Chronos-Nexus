#pragma once

#include <vector>
#include <functional>
#include "../graph/GalaxyGraph.h"

struct RouteWeights {
    double distance = 1.0;
    double fuel = 0.0;
    double time = 0.0;
    double risk = 0.0;
    double reliability = 0.0;
};

struct RouteMetrics {
    double distance = 0;
    double fuel = 0;
    double time = 0;
    double risk = 0;
};

struct PathResult {
    bool found = false;
    double totalCost = 0.0;
    std::vector<int> path;
    int nodesExplored = 0;
    std::vector<int> routeIds;
    RouteMetrics metrics;
};

class Dijkstra {
public:
    static PathResult findShortestPath(
        const GalaxyGraph& graph,
        int start,
        int destination,
        const RouteWeights& weights = {},
        double cargo = 0
    );
    static bool measure(const GalaxyGraph& graph,
        const std::vector<int>& routeIds, int origin, int destination,
        double cargo, RouteMetrics& metrics);
    static PathResult findNearest(const GalaxyGraph& graph, int start,
        const std::function<bool(int)>& isDestination,
        const RouteWeights& weights, double cargo = 0);
};
