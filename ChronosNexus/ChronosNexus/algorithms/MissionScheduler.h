#pragma once

#include <vector>
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
};

struct SchedulingResult {
    std::vector<ScheduledMission> scheduled;
    std::vector<int> rejectedMissionIds;
    int missionsScheduled = 0;
};

class MissionScheduler {
public:
    static SchedulingResult schedule(
        std::vector<Mission> missions,
        std::vector<Spacecraft> spacecraft,
        const GalaxyGraph& galaxy
    );
};


