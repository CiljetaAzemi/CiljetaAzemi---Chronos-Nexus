#include "TestSupport.h"
#include "../scenario/ScenarioJson.h"
#include "../third_party/nlohmann/json.hpp"

#include <sstream>
#include <string>
#include <cmath>

namespace regression {
namespace {
using Json = nlohmann::json;

scenario::Input parse(const std::string& text) {
    std::istringstream source(text);
    return scenario::loadJson(source);
}

void requireInvalid(const std::string& input, const std::string& expected) {
    try {
        parse(input);
    } catch (const std::invalid_argument& error) {
        require(std::string(error.what()).find(expected) != std::string::npos,
            "Scenario validation reported the wrong error");
        return;
    }
    require(false, "Invalid scenario was accepted");
}
}

void testScenarioJson() {
    const auto fixture = Json::parse(R"({
      "version": 1, "budget": 0, "horizon": 3,
      "locations": [{"id": 1, "type": "planet"}, {"id": 2, "type": "station"}],
      "routes": [{"id": 10, "from": 1, "to": 2, "distance": 4, "travel_time": 1, "fuel": 1, "capacity": 4}],
      "inventory": [
        {"location": 1, "resource": "water", "stock": 4, "target": 0, "storage": 8},
        {"location": 2, "resource": "water", "stock": 0, "target": 3, "storage": 8}
      ],
      "spacecraft": [{"id": 9, "location": 1, "cargo_capacity": 4, "fuel_capacity": 4, "fuel": 4, "speed": 1}],
      "missions": [{"id": 2, "origin": 1, "destination": 2, "resource": "water", "amount": 3, "deadline": 2, "priority": 4}],
      "events": [{"id": 8, "time": 0, "type": "route_blocked", "target": 10}]
    })");
    const auto input = parse(fixture.dump());
    require(input.data.stations.size() == 1 && input.data.missions.front().cargoType == ResourceType::Water,
        "JSON scenario was not mapped to the existing models");
    std::ostringstream exported;
    scenario::writeResult(input, exported);
    const auto result = Json::parse(exported.str());
    const auto& plan = result.at("planned");
    require(plan.at("assignments").size() == 1 &&
        plan.at("assignments").at(0).at("route_ids") == Json::array({10}) &&
        plan.at("assignments").at(0).at("spacecraft_id") == 9 &&
        plan.at("forecast").at("completed") == 1, "Expected a feasible nominal JSON assignment");
    require(result.at("simulation").at("completed") == 0 && result.at("simulation").at("failed") == 1 &&
        result.at("simulation").at("missions").at(0).at("status") == "failed" &&
        !result.at("simulation").at("missions").at(0).contains("arrival"),
        "Export mixed forecast and realized outcomes");
    const auto& repair = result.at("simulation").at("replanning_decisions").at(0);
    require(repair.at("reason") == "blocked_route" && repair.at("previous").at("route_ids") == Json::array({10}) &&
        repair.at("replacement").is_null() && repair.at("recovered") == false,
        "Export must retain the invalidated assignment and unsuccessful recovery.");

    auto urgent = fixture;
    auto mission = urgent["missions"][0];
    urgent["missions"] = Json::array();
    urgent["events"][0] = {{"id", 8}, {"time", 1}, {"type", "urgent_mission"}, {"mission", mission}};
    std::ostringstream urgentOutput;
    scenario::writeResult(parse(urgent.dump()), urgentOutput);
    const auto urgentResult = Json::parse(urgentOutput.str());
    const auto& urgentRepair = urgentResult.at("simulation").at("replanning_decisions").at(0);
    require(urgentResult.at("planned").at("assignments").empty() &&
        urgentRepair.at("previous").is_null() && urgentRepair.at("recovered") == true &&
        urgentRepair.at("replacement").at("departure") == 1 &&
        urgentResult.at("simulation").at("missions").at(0).at("kind") == "urgent" &&
        urgentResult.at("simulation").at("completed") == 1,
        "Urgent JSON missions must become visible only at their event time.");
    auto later = mission;
    later["id"] = 3;
    later["priority"] = 12;
    urgent["events"].push_back({{"id", 9}, {"time", 1.5}, {"type", "urgent_mission"}, {"mission", later}});
    std::ostringstream competingOutput;
    scenario::writeResult(parse(urgent.dump()), competingOutput);
    const auto competing = Json::parse(competingOutput.str());
    const auto& competingActual = competing.at("simulation");
    require(competing.at("planned").at("forecast") == urgentResult.at("planned").at("forecast") &&
        competingActual.at("completed") == 1 && competingActual.at("failed") == 1 &&
        competingActual.at("service_obligations") == 2 &&
        std::abs(competingActual.at("civilization").at("service").get<double>() - 0.25) < 1e-12,
        "An unserved external request must count in the denominator without entering the earlier plan.");
    urgent["events"].erase(1);
    urgent["missions"].push_back(mission);
    requireInvalid(urgent.dump(), "events[0]: duplicate id");

    requireInvalid("{", "Invalid scenario JSON");
    auto invalid = fixture;
    invalid["routes"][0]["to"] = 99;
    requireInvalid(invalid.dump(), "routes[0]: to: unknown location");
    invalid = fixture;
    invalid["spacecraft"][0]["fuel"] = "four";
    requireInvalid(invalid.dump(), "spacecraft[0]: fuel:");
    invalid = fixture;
    invalid["inventory"].push_back(fixture["inventory"][0]);
    requireInvalid(invalid.dump(), "inventory[2]: duplicate location/resource account");
    invalid = fixture;
    invalid["missions"][0]["resource"] = "food";
    requireInvalid(invalid.dump(), "missions[0]: mission requires inventory accounts");
    invalid = fixture;
    invalid["events"][0]["announced"] = true;
    requireInvalid(invalid.dump(), "events[0]: announced:");
}
}
