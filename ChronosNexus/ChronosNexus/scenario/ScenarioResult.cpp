#include "ScenarioJson.h"
#include "../algorithms/CivilizationPlanner.h"
#include "../algorithms/LogisticsPlanner.h"
#include "../benchmarks/Measurements.h"
#include "../third_party/nlohmann/json.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace scenario {
namespace {
using Json = nlohmann::json;

const char* resourceName(ResourceType type) {
    constexpr std::array names{"food", "water", "energy", "fuel", "medicine",
        "rare_minerals", "construction_materials"};
    return names.at(static_cast<size_t>(type));
}

const char* statusName(MissionStatus status) {
    constexpr std::array names{"pending", "scheduled", "in_progress", "completed",
        "failed", "delayed", "cancelled"};
    return names.at(static_cast<size_t>(status));
}

Json resourceOutcomes(const SimulationResult& result) {
    std::map<std::pair<int, ResourceType>, SimulationResult::ResourceOutcome> ordered;
    for (const auto& [location, resources] : result.resourceOutcomes)
        for (const auto& [type, balance] : resources)
            ordered.emplace(std::make_pair(location, type), balance);

    Json rows = Json::array();
    for (const auto& [key, balance] : ordered)
        rows.push_back({{"location", key.first}, {"resource", resourceName(key.second)},
            {"ending_shortage", balance.endingShortage}, {"unserved_consumption", balance.unserved}});
    return rows;
}

Json score(const CivilizationMetrics& metrics) {
    return {{"score", metrics.score}, {"security", metrics.security},
        {"critical_security", metrics.criticalSecurity}, {"service", metrics.service},
        {"infrastructure", metrics.infrastructure}, {"operating_penalty", metrics.operatingPenalty},
        {"waste_penalty", metrics.wastePenalty}, {"risk_penalty", metrics.riskPenalty},
        {"critical_ending_shortage", metrics.criticalEndingShortage},
        {"critical_unserved_consumption", metrics.criticalUnserved}};
}

std::vector<Mission> proposeAid(const benchmark::Scenario& data, const RouteWeights& weights,
    double horizon, Json& proposals) {
    std::map<ResourceType, std::vector<ResourceDemand>> byResource;
    for (const auto& demand : data.aidDemands)
        byResource[demand.type].push_back(demand);

    long long nextId = 0;
    for (const auto& mission : data.missions)
        nextId = std::max(nextId, static_cast<long long>(mission.id) + 1);
    std::vector<Mission> aid;
    for (const auto& [type, demands] : byResource) {
        const auto allocation = ResourceAllocator::allocateWithRoutes(demands, data.graph, weights);
        if (allocation.transfers.size() > static_cast<size_t>(std::numeric_limits<int>::max() - nextId))
            throw std::invalid_argument("Mission IDs leave insufficient room for generated aid missions.");
        const auto generated = LogisticsPlanner::createMissions(allocation, demands, type,
            horizon, static_cast<int>(nextId));
        nextId += generated.size();
        // Positive transfers become aid missions in the same order.
        size_t index = 0;
        for (const auto& transfer : allocation.transfers) {
            if (!std::isfinite(transfer.amount) || transfer.amount <= 0)
                continue;
            proposals.push_back({{"mission_id", generated.at(index++).id},
                {"origin", transfer.fromPlanet}, {"destination", transfer.toPlanet},
                {"resource", resourceName(type)}, {"amount", transfer.amount},
                {"transportation_cost", transfer.transportationCost},
                {"locations", transfer.path}, {"route_ids", transfer.routeIds}});
        }
        aid.insert(aid.end(), generated.begin(), generated.end());
    }
    return aid;
}
}

void writeResult(Input input, std::ostream& destination) {
    benchmark::Measurements measurements;
    auto& data = input.data;
    const RouteWeights weights{0.02, 0.03, 0.5, 3.0, 2.0};
    Json proposals = Json::array();
    const auto aid = proposeAid(data, weights, input.horizon, proposals);
    std::unordered_set<int> colonies;
    for (const auto& colony : data.colonies)
        colonies.insert(colony.id);
    const CivilizationObjective objective(data.inventory, data.missions, data.projects, colonies,
        input.horizon, input.budget, CivilizationObjective::parseWeights("balanced"));
    std::unordered_set<int> aidIds;
    for (const auto& mission : aid)
        aidIds.insert(mission.id);
    data.missions.insert(data.missions.end(), aid.begin(), aid.end());
    const InventoryPlanningInput inventory{data.inventory, data.aidDemands, aid};
    auto initial = MissionScheduler::schedule(data.missions, data.ships, data.graph, weights, false, {}, &inventory);
    const auto plan = CivilizationPlanner::choose(std::move(initial), data.missions, aid, data.inventory,
        data.aidDemands, data.ships, data.graph, data.projects, objective, colonies,
        input.budget, input.horizon, weights, input.knownProductionChanges);
    const auto& schedule = plan.schedule;
    const auto& construction = plan.construction;
    std::map<int, const Mission*> missions;
    for (auto& mission : data.missions) {
        if (const auto origin = schedule.revisedOrigins.find(mission.id); origin != schedule.revisedOrigins.end())
            mission.origin = origin->second;
        missions.emplace(mission.id, &mission);
    }

    const auto builds = CivilizationPlanner::constructionEvents(data.projects, construction);
    auto nominalEvents = input.knownProductionChanges;
    nominalEvents.insert(nominalEvents.end(), builds.begin(), builds.end());
    data.events.insert(data.events.end(), builds.begin(), builds.end());
    const auto forecast = SimulationEngine::run(data.graph, data.inventory, data.missions, data.ships,
        schedule, nominalEvents, {}, false, input.horizon);
    const auto actual = SimulationEngine::run(data.graph, data.inventory, data.missions, data.ships,
        schedule, data.events, {}, false, input.horizon);

    Json assignments = Json::array();
    for (const auto& row : schedule.scheduled) {
        const auto& mission = *missions.at(row.missionId);
        assignments.push_back({{"mission_id", row.missionId}, {"spacecraft_id", row.spacecraftId},
            {"origin", mission.origin}, {"destination", mission.destination}, {"resource", resourceName(mission.cargoType)},
            {"amount", mission.cargoAmount}, {"departure", row.departureTime}, {"expected_arrival", row.arrivalTime},
            {"planned_fuel", row.fuelUsed}, {"locations", row.path}, {"route_ids", row.routeIds}});
    }
    Json projects = Json::array();
    for (const auto& project : data.projects) {
        const auto& selected = construction.selectedInfrastructureIds;
        if (std::find(selected.begin(), selected.end(), project.id) != selected.end())
            projects.push_back({{"project_id", project.id}, {"location", project.locationId},
                {"cost", project.constructionCost}, {"expected_completion", project.constructionTime},
                {"strategic_value", project.strategicValue}, {"food_production",
                 project.resourceImpact.contains(ResourceType::Food) ? project.resourceImpact.at(ResourceType::Food) : 0}});
    }
    Json outcomes = Json::array();
    for (const auto& [id, mission] : missions) {
        Json outcome{{"mission_id", id}, {"kind", aidIds.contains(id) ? "aid" : "input"}, {"deadline", mission->deadline}};
        const auto status = actual.missionStatuses.find(id);
        outcome["status"] = status == actual.missionStatuses.end() ? Json(nullptr) : Json(statusName(status->second));
        if (const auto delivery = actual.deliveries.find(id); delivery != actual.deliveries.end()) {
            outcome["delivered_amount"] = delivery->second.amount;
            outcome["arrival"] = delivery->second.arrival;
        }
        outcomes.push_back(std::move(outcome));
    }
    const Json nominal{
        {"completed", forecast.completed}, {"failed", forecast.failed},
        {"delivered_amount", forecast.deliveredAmount}, {"remaining_shortage", forecast.remainingShortage},
        {"resources", resourceOutcomes(forecast)}, {"civilization", score(objective.evaluate(forecast, construction))}
    };
    const Json planned{
        {"assignments", assignments}, {"rejected_mission_ids", schedule.rejectedMissionIds},
        {"recovered_mission_ids", schedule.recoveredMissionIds}, {"allocation_proposals", proposals},
        {"infrastructure", {{"budget", input.budget}, {"cost", construction.totalCost},
            {"strategic_value", construction.totalStrategicValue}, {"selections", projects}}},
        {"forecast", nominal}
    };
    const Json simulation{
        {"completed", actual.completed}, {"failed", actual.failed}, {"scheduled", actual.scheduled},
        {"inventory_failures", actual.inventoryFailures}, {"events_processed", actual.eventsProcessed},
        {"unsupported_events", actual.unsupportedEvents}, {"rerouted", actual.rerouted}, {"replans", actual.replans},
        {"distance", actual.distance}, {"fuel", actual.fuel}, {"risk", actual.risk}, {"waste", actual.waste},
        {"transportation_cost", actual.transportationCost}, {"delivered_amount", actual.deliveredAmount},
        {"remaining_shortage", actual.remainingShortage}, {"stockout_amount", actual.stockoutAmount},
        {"missions", outcomes}, {"resources", resourceOutcomes(actual)},
        {"civilization", score(objective.evaluate(actual, construction))}
    };
    const Json result{
        {"version", 1}, {"horizon", input.horizon}, {"objective_profile", "balanced"},
        {"planned", planned}, {"simulation", simulation}, {"processing_seconds", measurements.elapsed()}
    };
    destination << result.dump(2) << '\n';
}

int runFile(int argc, char* argv[]) {
    if (argc != 5 || std::string(argv[3]) != "--output" || !*argv[2] || !*argv[4])
        throw std::invalid_argument("Usage: --scenario-file INPUT.json --output RESULT.json");
    const std::filesystem::path inputPath(argv[2]), outputPath(argv[4]);
    std::ifstream source(inputPath);
    if (!source)
        throw std::runtime_error("Cannot open scenario file: " + inputPath.string());
    if (std::filesystem::exists(outputPath) && std::filesystem::equivalent(inputPath, outputPath))
        throw std::invalid_argument("Scenario file and output file must be different.");
    std::ostringstream result;
    writeResult(loadJson(source), result);
    std::ofstream destination(outputPath);
    if (!destination)
        throw std::runtime_error("Cannot open output file: " + outputPath.string());
    destination << result.str();
    destination.close();
    if (!destination)
        throw std::runtime_error("Cannot write output file: " + outputPath.string());
    return 0;
}
}
