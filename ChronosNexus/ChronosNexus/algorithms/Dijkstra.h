#pragma once

#include <vector>
#include "../graph/GalaxyGraph.h"

struct RouteWeights {
    double distance = 1.0;
    double fuel = 0.0;
    double time = 0.0;
    double risk = 0.0;
};

struct PathResult {
    bool found = false;
    double totalCost = 0.0;
    std::vector<int> path;
    int nodesExplored = 0;
};

class Dijkstra {
public:
    static PathResult findShortestPath(
        const GalaxyGraph& graph,
        int start,
        int destination,
        const RouteWeights& weights = {}
    );
};
