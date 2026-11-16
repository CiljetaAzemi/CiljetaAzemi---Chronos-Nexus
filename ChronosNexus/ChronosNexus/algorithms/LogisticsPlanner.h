
#pragma once

#include <vector>
#include <cmath>
#include "ResourceAllocator.h"
#include "MissionScheduler.h"

struct LogisticsPlan {
    AllocationResult allocation;
    std::vector<Mission> missions;
    SchedulingResult scheduling;
};

class LogisticsPlanner {
public:
    static LogisticsPlan createPlan(
        std::vector<ResourceDemand> demands,
        std::vector<Spacecraft> spacecraft,
        const GalaxyGraph& galaxy,
        ResourceType resourceType,
        double deadline,
        const RouteWeights& weights = {}
    ) {
        LogisticsPlan plan;

        // Step 1: Plan resource transfers.
        plan.allocation =
            ResourceAllocator::allocateWithRoutes(
                demands, galaxy, weights
            );

        // Step 2: Convert transfers to Mission objects.
        int nextMissionId = 1;

        for (const ResourceTransfer& transfer :
            plan.allocation.transfers) {

            if (!std::isfinite(transfer.amount) ||
                transfer.amount <= 0.0)
                continue;

            Mission mission{};

            mission.id = nextMissionId++;
            mission.origin = transfer.fromPlanet;
            mission.destination = transfer.toPlanet;
            mission.cargoType = resourceType;
            mission.cargoAmount = transfer.amount;
            mission.deadline = deadline;

            // Use the receiver's priority.
            for (const ResourceDemand& demand : demands) {
                if (demand.planetId == transfer.toPlanet) {
                    mission.priority = demand.priority;
                    break;
                }
            }

            mission.status = MissionStatus::Pending;

            plan.missions.push_back(mission);
        }

        // Step 3: Assign spacecraft to delivery missions.
        plan.scheduling = MissionScheduler::schedule(
            plan.missions,
            spacecraft,
            galaxy
        );

        return plan;
    }
};
