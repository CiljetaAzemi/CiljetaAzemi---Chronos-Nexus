#pragma once

#include <string>
#include <vector>

class Fleet {
public:
    // Identification
    int id = -1;
    std::string name;

    // Spacecraft belonging to the fleet
    std::vector<int> spacecraftIds;

    // Fleet base or shared location
    int currentLocation = -1;

    // Operational status
    bool isActive = true;

    // Assigned missions
    std::vector<int> assignedMissionIds;
};
