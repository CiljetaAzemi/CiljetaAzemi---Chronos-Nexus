#pragma once

#include <string>
#include <vector>
#include "../algorithms/ResourceAllocator.h"
#include "../models/Colony.h"
#include "../models/Event.h"
#include "../models/Infrastructure.h"
#include "../models/Mission.h"
#include "../models/Spacecraft.h"
#include "../models/Station.h"

namespace benchmark {
struct Size {
    int planets;
    int routes;
    int missions;
};

struct Scenario {
    GalaxyGraph graph;
    std::vector<Planet> planets;
    std::vector<Colony> colonies;
    std::vector<Station> stations;
    std::vector<ResourceDemand> inventory;
    std::vector<ResourceDemand> aidDemands;
    std::vector<Spacecraft> ships;
    std::vector<Mission> missions;
    std::vector<Infrastructure> projects;
    std::vector<Event> events;
    long long population = 0;

    std::unordered_map<int, int> populationByLocation() const {
        std::unordered_map<int, int> people;
        for (const auto& planet : planets)
            people.emplace(planet.id, planet.population);
        for (const auto& colony : colonies)
            people.emplace(colony.id, colony.population);
        return people;
    }
};

Size dimensions(const std::string& name);
Scenario generate(Size size, unsigned seed);
}
