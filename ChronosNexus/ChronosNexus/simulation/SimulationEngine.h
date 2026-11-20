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
    int inventoryFailures = 0;
    int replans = 0;
    double replanSeconds = 0;
    long long nodesExplored = 0;
    double distance = 0;
    double fuel = 0;
    double transportationCost = 0;
    double risk = 0;
    double waste = 0;
    double stockoutAmount = 0;

    double deliveredAmount = 0.0;
    double remainingShortage = 0.0;

    std::unordered_map<int, double> finalResources;
    struct ResourceOutcome { double endingShortage; double unserved; };
    std::unordered_map<int, std::unordered_map<ResourceType, ResourceOutcome>> resourceOutcomes;
    std::unordered_map<int, MissionStatus> missionStatuses;
    struct Delivery { double amount; double arrival; };
    std::unordered_map<int, Delivery> deliveries;
};

class SimulationEngine {
public:
    static SimulationResult run(
        const GalaxyGraph& galaxy,
        const std::vector<ResourceDemand>& planets,
        const std::vector<Mission>& missions,
        const std::vector<Spacecraft>& spacecraft,
        const SchedulingResult& schedule,
        const std::vector<Event>& events,
        const std::function<void(size_t)>& progress = {},
        bool recheckAllDepartures = false,
        double minimumHorizon = 0
    );
};

