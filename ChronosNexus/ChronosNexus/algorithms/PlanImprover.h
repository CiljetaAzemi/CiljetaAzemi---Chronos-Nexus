#pragma once

#include "MissionScheduler.h"

class PlanImprover {
public:
    static SchedulingResult improve(SchedulingResult plan, const std::vector<Mission>& missions,
        const std::vector<Spacecraft>& ships, const GalaxyGraph& graph,
        const std::vector<ResourceDemand>& inventory, const RouteWeights& weights, double horizon);
};
