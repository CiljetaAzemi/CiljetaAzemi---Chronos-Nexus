#pragma once
#include <string>
#include <unordered_map>
#include"Resource.h"

class Planet {
public:
    // Identification
    int id;
    std::string name;

    // Position in the galaxy
    double x;
    double y;

    // Population
    int population;

    // Resources
    //e kena perdor unoredered_map se kena multiple resource types
    std::unordered_map<ResourceType, Resource> resources;


    // Development and security
    int technologyLevel;
    int defenseLevel;
    double strategicImportance;
};
