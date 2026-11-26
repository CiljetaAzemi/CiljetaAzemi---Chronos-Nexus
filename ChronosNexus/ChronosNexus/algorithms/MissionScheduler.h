#pragma once

#include <vector>
#include <functional>
#include "../models/Mission.h"
#include "../models/Spacecraft.h"
#include "../graph/GalaxyGraph.h"
#include "Dijkstra.h"
#include "ResourceAllocator.h"

struct FlightEstimate {
    double earliestArrival = 0;
    double expectedArrival = 0;
    double latestArrival = 0;
    double routeReliability = 1;
    double risk = 0;

    static FlightEstimate from(const RouteMetrics& route, double departure, double speed) {
        return {departure + route.minimumTime / speed, departure + route.expectedTime / speed,
            departure + route.time / speed, route.reliability, route.risk};
    }
};

struct ScheduledMission {
    int missionId;
    int spacecraftId;
    double departureTime;
    double arrivalTime;
    double fuelUsed;
    std::vector<int> path;
    std::vector<int> routeIds;
    RouteWeights weights;
    FlightEstimate estimate;
};

struct SchedulingResult {
    std::vector<ScheduledMission> scheduled;
    std::vector<int> rejectedMissionIds;
    int missionsScheduled = 0;
    long long nodesExplored = 0;
    long long candidates = 0;
    std::vector<int> recoveredMissionIds;
    std::unordered_map<int, int> revisedOrigins;
    long long reservationChecks = 0;
    size_t routeCacheHits = 0;
    size_t routeCachePeakEntries = 0;
    size_t routeCachePeakPayloadBytes = 0;
    size_t routeCacheInvalidations = 0;
    int searchTrials = 0;
    int acceptedMoves = 0;
    int locallyRecovered = 0;
    long long searchArcs = 0;
    long long prunedArcs = 0;
    bool interrupted = false;
};

struct InventoryPlanningInput {
    const std::vector<ResourceDemand>& balances;
    const std::vector<ResourceDemand>& aidNetwork;
    const std::vector<Mission>& aidMissions;
    bool cacheRoutes = true;
};

class MissionScheduler {
public:
    static SchedulingResult schedule(
        std::vector<Mission> missions,
        std::vector<Spacecraft> spacecraft,
        const GalaxyGraph& galaxy,
        const RouteWeights& weights = {},
        bool earliestArrival = false,
        const std::function<void(size_t)>& progress = {},
        const InventoryPlanningInput* inventory = nullptr,
        bool benefitAware = false,
        const std::function<void(const SchedulingResult&)>& publishPrefix = {}
    );
    static bool feasible(const Mission& mission, const Spacecraft& ship,
        double departure, const RouteMetrics& route, double& arrival);
    static double serviceValue(const Mission& mission);
    static double bookingValue(const Mission& mission, const Spacecraft& ship, const RouteMetrics& route);
};


