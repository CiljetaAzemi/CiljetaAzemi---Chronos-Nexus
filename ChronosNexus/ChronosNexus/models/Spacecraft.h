#pragma once

#include <string>

class Spacecraft {
public:
    // Identification
    int id;
    std::string name;

    // Current position
    int currentLocation;

    // Cargo and fuel capacities
    double maxCargoCapacity;
    double maxFuelCapacity;

    // Movement
    double speed;
    double currentFuel;

    // Scheduling
    double availableAt;

    // Reliability and cost
    double reliability;
    double operatingCost;
};
