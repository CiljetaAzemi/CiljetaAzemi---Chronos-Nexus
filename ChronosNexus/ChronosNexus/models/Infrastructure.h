#pragma once

#include <unordered_map>
#include "Resource.h"

// Types of infrastructure
enum class InfrastructureType {
    EnergyPlant,
    Farm,
    Hospital,
    Factory,
    DefenseSystem,
    ResearchStation,
    Spaceport,
    MiningFacility
};

// Construction status
enum class ConstructionStatus {
    Proposed,
    UnderConstruction,
    Completed,
    Cancelled
};

class Infrastructure {
public:
    // Identification
    int id = -1;
    InfrastructureType type = InfrastructureType::Farm;

    // Location
    int locationId = -1;

    // Construction requirements
    double constructionCost = 0.0;
    double constructionTime = 0.0;

    // Impact on civilization
    std::unordered_map<ResourceType, double> resourceImpact;
    double populationImpact = 0.0;
    double energyImpact = 0.0;
    double strategicValue = 0.0;

    // Construction progress
    ConstructionStatus status =
        ConstructionStatus::Proposed;
};
