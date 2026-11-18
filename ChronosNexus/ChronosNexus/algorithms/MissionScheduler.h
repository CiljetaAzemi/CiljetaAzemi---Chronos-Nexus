#pragma once

#include <vector>
#include <functional>
#include "../models/Mission.h"
#include "../models/Spacecraft.h"
#include "../graph/GalaxyGraph.h"
#include "Dijkstra.h"

struct ScheduledMission {
    int missionId;
    int spacecraftId;
    double departureTime;
    double arrivalTime;
    double fuelUsed;
    std::vector<int> path;
    std::vector<int> routeIds;
    RouteWeights weights;
};

struct SchedulingResult {
    std::vector<ScheduledMission> scheduled;
    std::vector<int> rejectedMissionIds;
    int missionsScheduled = 0;
    long long nodesExplored = 0;
    long long candidates = 0;
};

class MissionScheduler {
public:
    static SchedulingResult schedule(
        std::vector<Mission> missions,
        std::vector<Spacecraft> spacecraft,
        const GalaxyGraph& galaxy,
        const RouteWeights& weights = {},
        bool earliestArrival = false,
        const std::function<void(size_t)>& progress = {}
    );
    static bool feasible(const Mission& mission, const Spacecraft& ship,
        double departure, const RouteMetrics& route, double& arrival);
};


