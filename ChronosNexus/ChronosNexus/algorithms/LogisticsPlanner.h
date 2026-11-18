#pragma once

#include <cmath>
#include <unordered_map>
#include "ResourceAllocator.h"
#include "MissionScheduler.h"

struct LogisticsPlan {
    AllocationResult allocation;
    std::vector<Mission> missions;
    SchedulingResult scheduling;
};

class LogisticsPlanner {
public:
    static std::vector<Mission> createMissions(const AllocationResult& allocation,
        const std::vector<ResourceDemand>& demands, ResourceType resourceType,
        double deadline, int nextMissionId = 1) {
        std::unordered_map<int, const ResourceDemand*> receivers;
        for (const auto& demand : demands)
            if (demand.type == resourceType)
                receivers.emplace(demand.planetId, &demand);

        std::vector<Mission> missions;
        for (const auto& transfer : allocation.transfers) {
            if (!std::isfinite(transfer.amount) || transfer.amount <= 0)
                continue;
            Mission mission;
            mission.id = nextMissionId++;
            mission.origin = transfer.fromPlanet;
            mission.destination = transfer.toPlanet;
            mission.cargoType = resourceType;
            mission.cargoAmount = transfer.amount;
            mission.deadline = deadline;
            const auto& receiver = *receivers.at(transfer.toPlanet);
            mission.priority = receiver.priority;
            const auto netConsumption = receiver.consumption - receiver.production;
            if (netConsumption > 0)
                mission.deadline = std::min(deadline, receiver.available / netConsumption);
            missions.push_back(mission);
        }
        return missions;
    }

    static LogisticsPlan createPlan(std::vector<ResourceDemand> demands,
        std::vector<Spacecraft> spacecraft, const GalaxyGraph& galaxy,
        ResourceType resourceType, double deadline, const RouteWeights& weights = {}) {
        for (auto& demand : demands)
            demand.type = resourceType; // This convenience entry point plans one resource.
        LogisticsPlan plan;
        plan.allocation = ResourceAllocator::allocateWithRoutes(demands, galaxy, weights);
        plan.missions = createMissions(plan.allocation, demands, resourceType, deadline);
        plan.scheduling = MissionScheduler::schedule(plan.missions, spacecraft, galaxy, weights);
        return plan;
    }
};
