#pragma once
#include "Resource.h"
#include "Mission.h"
#include <optional>

// Types of dynamic events
enum class EventType {
    WormholeCollapse,
    RouteBlocked,
    PirateBlockade,
    SpacecraftDestroyed,
    EmergencyMedicalRequest,
    ResourceProductionDecrease,
    NewColonyDiscovered,
    PopulationIncrease,
    ResourceUnavailable,
    HighPriorityMission,
    RouteRiskIncrease,
    SpacecraftMechanicalFailure,
    ConstructionCompleted
};

// Types of objects affected by events
enum class AffectedObjectType {
    Route,
    Planet,
    Colony,
    Spacecraft,
    Mission,
    Resource
};

class Event {
public:
    // Identification
    int id = -1;

    // Event information
    double eventTime = 0.0;
    EventType eventType = EventType::RouteBlocked;

    // Affected object
    AffectedObjectType affectedObjectType =
        AffectedObjectType::Route;

    int affectedObjectId = -1;

    // Event severity
    double severity = 0.0;

    // Additional event parameters
    ResourceType resourceType = ResourceType::Food;
    double changeAmount = 0.0;
    double duration = 0.0;
    std::optional<Mission> urgentMission;
};
