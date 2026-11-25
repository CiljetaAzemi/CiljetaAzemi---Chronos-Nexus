#pragma once

#include "CivilizationObjective.h"

struct CivilizationPlan {
    SchedulingResult schedule;
    InfrastructureOptimizationResult construction;
    double forecastScore = 0;
    int evaluated = 0;
    int feasible = 0;
};

struct CoordinatedAid {
    std::vector<Mission> missions;
    AllocationResult allocation;
    SimulationResult::AllocationWork work;
    bool retained = false;
};

class CivilizationPlanner {
public:
    static CoordinatedAid coordinateAid(CivilizationPlan& plan, const std::vector<Mission>& obligations,
        const std::vector<ResourceDemand>& accounts, const std::vector<ResourceDemand>& network,
        const std::vector<Spacecraft>& ships, const GalaxyGraph& graph, const CivilizationObjective& objective,
        const std::vector<Event>& knownSupply, double horizon, int firstAidId);
    // Construction events are generated from committed projects.
    static std::vector<Event> constructionEvents(const std::vector<Infrastructure>& projects,
        const InfrastructureOptimizationResult& construction);

    static CivilizationPlan choose(SchedulingResult initial, const std::vector<Mission>& obligations,
        const std::vector<Mission>& aid, const std::vector<ResourceDemand>& inventory,
        const std::vector<ResourceDemand>& aidNetwork, const std::vector<Spacecraft>& ships,
        const GalaxyGraph& graph, const std::vector<Infrastructure>& projects,
        const CivilizationObjective& objective, const std::unordered_set<int>& colonies,
        int budget, double horizon, const RouteWeights& weights,
        const std::vector<Event>& knownProductionChanges = {});
};
