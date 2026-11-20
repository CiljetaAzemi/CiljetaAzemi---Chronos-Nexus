#include "DatasetGenerator.h"
#include <algorithm>
#include <random>
#include <stdexcept>
#include <utility>

namespace benchmark {
Size dimensions(const std::string& name) {
    if (name == "small") return {100, 500, 50};
    if (name == "medium") return {1000, 10000, 500};
    if (name == "large") return {10000, 100000, 5000};
    if (name == "extreme") return {100000, 1000000, 50000};
    throw std::invalid_argument("Expected small, medium, large, or extreme.");
}

Scenario generate(Size size, unsigned seed) {
    std::mt19937 random(seed);
    auto draw = [&](unsigned limit) { return static_cast<int>(random() % limit); };
    Scenario data;
    data.planets.reserve(size.planets);
    data.inventory.reserve(size.planets * 2);
    for (int i = 0; i < size.planets; ++i) {
        Planet planet{};
        planet.id = i;
        planet.population = 1000 + draw(100000);
        planet.x = i % 1000;
        planet.y = i / 1000;
        planet.technologyLevel = 1 + draw(8);
        planet.defenseLevel = draw(10);
        planet.strategicImportance = 1 + draw(10);
        data.population += planet.population;
        const bool receiver = i % 25 == 0;
        const bool donor = i % 25 == 1;
        ResourceDemand food{i, receiver ? 12.0 : donor ? 220.0 : 80.0,
            45.0, static_cast<int>(planet.strategicImportance), ResourceType::Food,
            receiver ? 0.5 : 2.0, receiver ? 2.0 : 1.5, 250.0};
        ResourceDemand medicine{i, 20.0 + draw(50), 15.0, food.priority,
            ResourceType::Medicine, 0.3, 0.4, 100.0};
        for (const auto& resource : {food, medicine}) {
            planet.resources.emplace(resource.type, Resource{resource.type, resource.available,
                resource.production, resource.consumption, resource.storage, 1.0});
            data.inventory.push_back(resource);
        }
        if (receiver || donor)
            data.aidDemands.push_back(food);
        data.planets.push_back(std::move(planet));
        data.graph.addLocation(i);
    }

    const int colonyCount = std::max(1, size.planets / 100);
    const int stationCount = std::max(1, size.planets / 200);
    for (int i = 0; i < colonyCount; ++i) {
        Colony colony;
        static_cast<Planet&>(colony) = data.planets[i];
        colony.id = size.planets + i;
        colony.parentPlanetId = i;
        colony.isEstablished = true;
        colony.population /= 20;
        data.population += colony.population;
        data.inventory.push_back({colony.id, 30, 35, 6, ResourceType::Food, 0.5, 1.0, 100});
        colony.resources[ResourceType::Food] = {ResourceType::Food, 30, 0.5, 1.0, 100, 1};
        data.aidDemands.push_back(data.inventory.back());
        data.graph.addLocation(colony.id);
        data.colonies.push_back(std::move(colony));
    }
    for (int i = 0; i < stationCount; ++i) {
        Station station;
        station.id = size.planets + colonyCount + i;
        station.storageCapacity = 100;
        station.resources[ResourceType::Medicine] = 30;
        data.inventory.push_back({station.id, 30, 10, 4, ResourceType::Medicine, 0, 0, 100});
        data.graph.addLocation(station.id);
        data.stations.push_back(std::move(station));
    }

    auto connect = [&](int id, int from, int to, bool wormhole) {
        Route route{};
        route.id = id;
        route.from = from;
        route.to = to;
        route.distance = 5.0 + draw(wormhole ? 70 : 30);
        route.fuelConsumption = route.distance * (0.6 + draw(90) / 100.0);
        // Seeded edge durations form one deterministic scenario realization.
        route.travelTime = route.distance * (0.06 + draw(12) / 100.0);
        route.risk = (1 + draw(wormhole ? 30 : 12)) / 100.0;
        route.reliability = 1 - route.risk / 2;
        route.maxCapacity = 12 + draw(75);
        route.isAvailable = true;
        route.isWormhole = wormhole;
        data.graph.addRoute(route);
    };
    int edge = 0;
    for (int i = 0; i < size.planets; ++i)
        connect(edge++, i, (i + 1) % size.planets, false);
    for (const auto& colony : data.colonies) {
        connect(edge++, colony.parentPlanetId, colony.id, false);
        connect(edge++, colony.id, colony.parentPlanetId, false);
    }
    for (int i = 0; i < stationCount; ++i) {
        connect(edge++, i, data.stations[i].id, false);
        connect(edge++, data.stations[i].id, i, false);
    }
    while (edge < size.routes) {
        const int from = edge % size.planets;
        const bool wormhole = draw(100) < 5;
        const int offset = wormhole ? 1 + draw(size.planets - 1) : 1 + draw(40);
        const int id = edge++;
        connect(id, from, (from + offset) % size.planets, wormhole);
    }

    auto addShip = [&](int origin) {
        Spacecraft ship{};
        ship.id = static_cast<int>(data.ships.size());
        ship.currentLocation = origin;
        ship.maxCargoCapacity = 18 + draw(63);
        ship.maxFuelCapacity = 190 + draw(220);
        ship.currentFuel = ship.maxFuelCapacity * (0.55 + draw(46) / 100.0);
        ship.speed = 0.7 + draw(100) / 100.0;
        ship.availableAt = draw(5) / 2.0;
        ship.reliability = 0.84 + draw(17) / 100.0;
        ship.operatingCost = 0.5 + draw(100) / 100.0;
        data.ships.push_back(ship);
    };
    for (int i = 0; i < size.missions; ++i) {
        Mission mission;
        mission.id = i;
        mission.origin = draw(size.planets);
        mission.destination = (mission.origin + 1 + draw(i % 8 == 0 ? size.planets - 1 : 48)) % size.planets;
        mission.cargoType = i % 3 == 0 ? ResourceType::Medicine : ResourceType::Food;
        mission.cargoAmount = 6 + draw(55);
        mission.deadline = 4 + draw(25);
        mission.priority = 1 + draw(10);
        mission.requirements.minReliability = i % 5 == 0 ? 0.95 : 0.8;
        if (i % 7 == 0) {
            const int station = i % stationCount;
            mission.origin = station;
            mission.destination = data.stations[station].id;
            mission.cargoType = ResourceType::Medicine;
        }
        data.missions.push_back(mission);
        if (i % 4 != 0)
            addShip(mission.origin);
    }
    for (int i = 1; i < size.planets; i += 25)
        addShip(i);

    for (int i = 0; i < std::max(8, size.planets / 100); ++i) {
        Infrastructure p;
        p.id = i;
        p.locationId = (i * 25) % size.planets;
        p.constructionCost = 5 + draw(40);
        p.constructionTime = 1 + draw(12);
        p.strategicValue = 5 + draw(80);
        p.resourceImpact[ResourceType::Food] = 0.5 + draw(25) / 10.0;
        data.projects.push_back(std::move(p));
    }
    for (int i = 0; i < std::max(12, size.missions / 50); ++i) {
        Event e;
        e.id = i;
        e.eventTime = draw(9);
        if (i % 3 == 0) {
            e.eventType = EventType::RouteBlocked;
            e.affectedObjectId = draw(size.routes);
        } else if (i % 3 == 1) {
            e.eventType = EventType::SpacecraftDestroyed;
            e.affectedObjectType = AffectedObjectType::Spacecraft;
            e.affectedObjectId = draw(static_cast<unsigned>(data.ships.size()));
        } else {
            e.eventType = EventType::ResourceProductionDecrease;
            e.affectedObjectType = AffectedObjectType::Planet;
            e.affectedObjectId = 25 * draw(size.planets / 25);
            e.severity = 0.6;
        }
        data.events.push_back(e);
    }
    return data;
}
}
