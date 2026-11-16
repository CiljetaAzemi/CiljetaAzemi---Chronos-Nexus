#pragma once

#include <vector>
#include <unordered_map>

#include "../models/Event.h"
#include "../models/Mission.h"
#include "../models/Spacecraft.h"
#include "../graph/GalaxyGraph.h"
#include "../algorithms/ResourceAllocator.h"
#include "../algorithms/MissionScheduler.h"

struct SimulationResult {
    int completed = 0;
    int failed = 0;
    int scheduled = 0;
    int eventsProcessed = 0;
    int unsupportedEvents = 0;
    int rerouted = 0;

    double deliveredAmount = 0.0;
    double remainingShortage = 0.0;

    std::unordered_map<int, double> finalResources;
    std::unordered_map<int, MissionStatus> missionStatuses;
};

class SimulationEngine {
public:
    static SimulationResult run(
        const GalaxyGraph& galaxy,
        const std::vector<ResourceDemand>& planets,
        const std::vector<Mission>& missions,
        const std::vector<Spacecraft>& spacecraft,
        const SchedulingResult& schedule,
        const std::vector<Event>& events
    );
};

