#pragma once

#include <string>
#include <unordered_map>
#include "Resource.h"

class Station {
public:
    // Identification
    int id = -1;
    std::string name;

    // Position in the galaxy
    double x = 0.0;
    double y = 0.0;

    // Stored resources
    std::unordered_map<ResourceType, double> resources;

    // Storage and docking
    double storageCapacity = 0.0;
    int maxDockedSpacecraft = 0;

    // Operational status
    bool isOperational = true;
};
