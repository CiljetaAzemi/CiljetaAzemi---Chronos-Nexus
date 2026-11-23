#include "ScenarioJson.h"
#include "../third_party/nlohmann/json.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace scenario {
namespace {
using Json = nlohmann::json;

void check(bool condition, const std::string& message) {
    if (!condition)
        throw std::invalid_argument(message);
}

template<class T>
T get(const Json& row, const char* key) {
    try {
        const auto& v = row.at(key);
        if constexpr (std::is_same_v<T, int>)
            check(v.is_number_integer() && v.get<double>() >= 0 &&
                v.get<double>() < std::numeric_limits<int>::max(), "expected a nonnegative integer below INT_MAX");
        if constexpr (std::is_same_v<T, double>)
            check(v.is_number() && std::isfinite(v.get<double>()), "expected a finite number");
        return v.get<T>();
    } catch (const std::exception& e) {
        throw std::invalid_argument(std::string(key) + ": " + e.what());
    }
}

template<class T>
T optional(const Json& row, const char* key, T fallback) {
    return row.contains(key) ? get<T>(row, key) : fallback;
}

double number(const Json& row, const char* key, double fallback = -1, double min = 0) {
    const double v = fallback < 0 ? get<double>(row, key) : optional(row, key, fallback);
    check(v >= min, std::string(key) + ": below allowed minimum");
    return v;
}

double probability(const Json& row, const char* key, double fallback = -1) {
    const double v = number(row, key, fallback);
    check(v <= 1, std::string(key) + ": expected a value in [0,1]");
    return v;
}

template<class F>
void rows(const Json& root, const char* key, F load, bool required = true) {
    if (!required && !root.contains(key))
        return;
    check(root.contains(key) && root.at(key).is_array(), std::string(key) + ": expected an array");
    size_t i = 0;
    for (const auto& row : root.at(key)) {
        try {
            check(row.is_object(), "expected an object");
            load(row);
        } catch (const std::exception& e) {
            throw std::invalid_argument(std::string(key) + "[" + std::to_string(i) + "]: " + e.what());
        }
        ++i;
    }
}

ResourceType resource(const Json& row) {
    static const std::map<std::string, ResourceType> types{
        {"food", ResourceType::Food}, {"water", ResourceType::Water}, {"energy", ResourceType::Energy},
        {"fuel", ResourceType::Fuel}, {"medicine", ResourceType::Medicine},
        {"rare_minerals", ResourceType::RareMinerals}, {"construction_materials", ResourceType::ConstructionMaterials}};
    const auto name = get<std::string>(row, "resource");
    const auto it = types.find(name);
    check(it != types.end(), "resource: unknown resource '" + name + "'");
    return it->second;
}

int idOf(const Json& row, std::set<int>& ids) {
    const int id = get<int>(row, "id");
    check(ids.insert(id).second, "duplicate id " + std::to_string(id));
    return id;
}
}

Input loadJson(std::istream& source) {
    Json root;
    try {
        root = Json::parse(source);
    } catch (const Json::exception& e) {
        throw std::invalid_argument(std::string("Invalid scenario JSON: ") + e.what());
    }
    check(root.is_object(), "Scenario must be a JSON object");
    check(get<int>(root, "version") == 1, "version: expected schema version 1");
    Input input;
    input.budget = get<int>(root, "budget");
    input.horizon = number(root, "horizon", 30);
    check(input.horizon > 0, "horizon: must be positive");

    auto& data = input.data;
    std::set<int> locations, routes, ships, missions, projects, events;
    std::set<std::pair<int, ResourceType>> accounts;
    auto location = [&](const Json& row, const char* key) {
        const int id = get<int>(row, key);
        check(locations.contains(id), std::string(key) + ": unknown location " + std::to_string(id));
        return id;
    };
    auto time = [&](const Json& row, const char* key, double fallback = -1) {
        const double v = number(row, key, fallback);
        check(v <= input.horizon, std::string(key) + ": exceeds horizon");
        return v;
    };
    rows(root, "locations", [&](const Json& row) {
        Planet p{};
        p.id = idOf(row, locations);
        p.name = optional(row, "name", std::string{});
        p.x = optional(row, "x", 0.0);
        p.y = optional(row, "y", 0.0);
        const auto type = get<std::string>(row, "type");
        check(type == "station" || type == "planet" || type == "colony", "type: expected planet, colony, or station");
        if (type == "station") {
            Station s;
            s.id = p.id;
            s.name = p.name;
            s.x = p.x;
            s.y = p.y;
            data.stations.push_back(std::move(s));
        } else {
            p.population = optional(row, "population", 0);
            p.technologyLevel = optional(row, "technology", 0);
            p.defenseLevel = optional(row, "defense", 0);
            p.strategicImportance = number(row, "strategic_importance", 0);
            data.population += p.population;
            if (type == "planet")
                data.planets.push_back(p);
            else {
                Colony c;
                static_cast<Planet&>(c) = p;
                if (row.contains("parent_planet"))
                    c.parentPlanetId = get<int>(row, "parent_planet");
                data.colonies.push_back(std::move(c));
            }
        }
        data.graph.addLocation(p.id);
    });
    check(!locations.empty(), "locations: at least one location is required");
    for (const auto& c : data.colonies)
        if (c.parentPlanetId >= 0)
            check(std::any_of(data.planets.begin(), data.planets.end(),
                [&](const Planet& p) { return p.id == c.parentPlanetId; }),
                "colony " + std::to_string(c.id) + ": unknown parent planet");

    rows(root, "routes", [&](const Json& row) {
        Route r{};
        r.id = idOf(row, routes);
        r.from = location(row, "from");
        r.to = location(row, "to");
        r.distance = number(row, "distance");
        r.travelTime = number(row, "travel_time");
        r.fuelConsumption = number(row, "fuel");
        r.maxCapacity = number(row, "capacity");
        r.reliability = probability(row, "reliability", 1);
        r.risk = probability(row, "risk", 0);
        r.isAvailable = optional(row, "available", true);
        r.isWormhole = optional(row, "wormhole", false);
        data.graph.addRoute(r);
    });
    rows(root, "inventory", [&](const Json& row) {
        ResourceDemand b{};
        b.planetId = location(row, "location");
        b.type = resource(row);
        check(accounts.emplace(b.planetId, b.type).second, "duplicate location/resource account");
        b.available = number(row, "stock");
        b.required = number(row, "target");
        b.storage = number(row, "storage");
        b.production = number(row, "production", 0);
        b.consumption = number(row, "consumption", 0);
        b.priority = optional(row, "priority", 0);
        check(b.priority <= std::numeric_limits<int>::max() - 8, "priority: too large for planning adjustments");
        check(b.available <= b.storage && b.required <= b.storage, "stock or target exceeds storage");
        data.inventory.push_back(b);
        if (optional(row, "aid", false))
            data.aidDemands.push_back(b);
    });
    rows(root, "spacecraft", [&](const Json& row) {
        Spacecraft s{};
        s.id = idOf(row, ships);
        s.name = optional(row, "name", std::string{});
        s.currentLocation = location(row, "location");
        s.maxCargoCapacity = number(row, "cargo_capacity");
        s.maxFuelCapacity = number(row, "fuel_capacity");
        s.currentFuel = number(row, "fuel");
        s.speed = number(row, "speed");
        check(s.speed > 0 && s.currentFuel <= s.maxFuelCapacity, "speed must be positive and fuel within capacity");
        s.reliability = probability(row, "reliability", 1);
        s.availableAt = time(row, "available_at", 0);
        s.operatingCost = number(row, "operating_cost", 0);
        data.ships.push_back(std::move(s));
    });
    auto readMission = [&](const Json& row) {
        check(row.is_object(), "mission: expected an object");
        Mission m;
        m.id = idOf(row, missions);
        m.origin = location(row, "origin");
        m.destination = location(row, "destination");
        m.cargoType = resource(row);
        check(accounts.contains({m.origin, m.cargoType}) && accounts.contains({m.destination, m.cargoType}),
            "mission requires inventory accounts at both endpoints");
        m.cargoAmount = number(row, "amount");
        check(m.cargoAmount > 0, "amount: must be positive");
        m.deadline = time(row, "deadline");
        m.priority = get<int>(row, "priority");
        check(m.priority <= std::numeric_limits<int>::max() - 8, "priority: too large for planning adjustments");
        m.importance = number(row, "importance", 0);
        m.requirements.minCargoCapacity = number(row, "min_cargo_capacity", 0);
        m.requirements.minFuelCapacity = number(row, "min_fuel_capacity", 0);
        m.requirements.minSpeed = number(row, "min_speed", 0);
        m.requirements.minReliability = probability(row, "min_reliability", 0);
        return m;
    };
    rows(root, "missions", [&](const Json& row) {
        data.missions.push_back(readMission(row));
    });
    rows(root, "projects", [&](const Json& row) {
        Infrastructure p;
        p.id = idOf(row, projects);
        p.locationId = location(row, "location");
        p.constructionCost = get<int>(row, "cost");
        p.constructionTime = time(row, "completion_time");
        p.strategicValue = number(row, "strategic_value");
        const double food = number(row, "food_production", 0);
        if (food > 0) {
            check(accounts.contains({p.locationId, ResourceType::Food}), "food production requires a food account");
            p.resourceImpact.emplace(ResourceType::Food, food);
        }
        data.projects.push_back(std::move(p));
    }, false);
    rows(root, "events", [&](const Json& row) {
        Event e;
        e.id = idOf(row, events);
        e.eventTime = time(row, "time");
        const auto type = get<std::string>(row, "type");
        const bool urgent = type == "urgent_mission" || type == "emergency_medical_request";
        if (!urgent)
            e.affectedObjectId = get<int>(row, "target");
        static const std::map<std::string, EventType> routeTypes{
            {"route_blocked", EventType::RouteBlocked}, {"wormhole_collapse", EventType::WormholeCollapse},
            {"pirate_blockade", EventType::PirateBlockade}, {"route_risk_increase", EventType::RouteRiskIncrease}};
        if (const auto it = routeTypes.find(type); it != routeTypes.end()) {
            check(routes.contains(e.affectedObjectId), "target: unknown route");
            e.eventType = it->second;
            if (e.eventType == EventType::RouteRiskIncrease)
                e.severity = probability(row, "severity");
        } else if (type == "ship_destroyed" || type == "ship_failure") {
            check(ships.contains(e.affectedObjectId), "target: unknown spacecraft");
            e.affectedObjectType = AffectedObjectType::Spacecraft;
            e.eventType = type == "ship_destroyed" ? EventType::SpacecraftDestroyed : EventType::SpacecraftMechanicalFailure;
        } else if (urgent) {
            e.eventType = type == "urgent_mission" ? EventType::HighPriorityMission : EventType::EmergencyMedicalRequest;
            e.urgentMission = readMission(row.at("mission"));
            check(e.urgentMission->deadline >= e.eventTime, "mission deadline precedes its event");
            check(type != "emergency_medical_request" || e.urgentMission->cargoType == ResourceType::Medicine,
                "emergency medical requests must deliver medicine");
            e.affectedObjectType = AffectedObjectType::Mission;
            e.affectedObjectId = e.urgentMission->id;
        } else if (type == "production_decrease") {
            e.eventType = EventType::ResourceProductionDecrease;
            e.affectedObjectType = AffectedObjectType::Planet;
            e.resourceType = resource(row);
            e.severity = probability(row, "severity");
            check(accounts.contains({e.affectedObjectId, e.resourceType}), "target: unknown inventory account");
        } else
            check(false, "type: unsupported simulator event '" + type + "'");
        if (optional(row, "announced", false)) {
            check(e.eventType == EventType::ResourceProductionDecrease,
                "announced: only production decreases can be forecast");
            input.knownProductionChanges.push_back(e);
        }
        data.events.push_back(e);
    }, false);
    return input;
}
}
