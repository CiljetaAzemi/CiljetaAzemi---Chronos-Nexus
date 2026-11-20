#pragma once

#include "CivilizationObjective.h"

struct CivilizationPlan {
    SchedulingResult schedule;
    InfrastructureOptimizationResult construction;
    double forecastScore = 0;
    int evaluated = 0;
    int feasible = 0;
};

class CivilizationPlanner {
public:
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
