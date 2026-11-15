#pragma once

#include <vector>
#include "Resource.h"

// Spacecraft capabilities required by a mission
struct MissionRequirements {
    double minCargoCapacity = 0.0;
    double minFuelCapacity = 0.0;
    double minSpeed = 0.0;
    double minReliability = 0.0;
};

enum class MissionStatus {
    Pending,
    Scheduled,
    InProgress,
    Completed,
    Failed,
    Delayed,
    Cancelled
};

class Mission {
public:
    // Identification
    int id = -1;

    // Origin and destination
    int origin = -1;
    int destination = -1;

    // Cargo
    ResourceType cargoType = ResourceType::Food;
    double cargoAmount = 0.0;

    // Scheduling constraints
    double deadline = 0.0;
    int priority = 0;
    double importance = 0.0;

    // Required spacecraft capabilities
    MissionRequirements requirements;

    // Scheduling decisions
    int assignedSpacecraftId = -1;
    double departureTime = -1.0;
    std::vector<int> plannedRoute;

    // Current mission state
    MissionStatus status = MissionStatus::Pending;
};
