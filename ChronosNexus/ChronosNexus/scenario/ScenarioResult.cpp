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
#include <iomanip>
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

const char* reasonName(SimulationResult::ReplanningDecision::Reason reason) {
    using Reason = SimulationResult::ReplanningDecision::Reason;
    switch (reason) {
    case Reason::BlockedRoute: return "blocked_route";
    case Reason::UnavailableSpacecraft: return "unavailable_spacecraft";
    case Reason::UrgentMission: return "urgent_mission";
    case Reason::DependentAssignment: return "dependent_assignment";
    case Reason::PreventiveSupply: return "preventive_supply";
    case Reason::IncreasedRouteRisk: return "increased_route_risk";
    case Reason::DisplacedBooking: return "displaced_booking";
    }
    throw std::logic_error("Unknown replanning reason");
}

Json flightEstimate(const FlightEstimate& estimate) {
    return {{"earliest_arrival", estimate.earliestArrival}, {"expected_arrival", estimate.expectedArrival},
        {"latest_arrival", estimate.latestArrival}, {"route_reliability", estimate.routeReliability},
        {"route_failure_probability", 1 - estimate.routeReliability}, {"risk", estimate.risk}};
}

Json flightDecision(const ScheduledMission& entry) {
    return {{"spacecraft_id", entry.spacecraftId}, {"departure", entry.departureTime},
        {"expected_arrival", entry.estimate.expectedArrival}, {"scheduled_arrival", entry.arrivalTime},
        {"planned_fuel", entry.fuelUsed},
        {"locations", entry.path}, {"route_ids", entry.routeIds}, {"uncertainty", flightEstimate(entry.estimate)}};
}

Json shortageForecast(const SimulationResult::ShortageForecast& row) {
    return {{"location", row.location}, {"resource", resourceName(row.resource)},
        {"calculated_at", row.calculatedAt}, {"ending_stock", row.endingStock},
        {"ending_shortage", row.endingShortage}, {"unserved", row.unserved},
        {"depletion_time", row.depletionTime ? Json(*row.depletionTime) : Json(nullptr)}};
}

Json shortagePrediction(const SimulationResult& result) {
    Json forecasts = Json::array();
    for (const auto& row : result.shortageForecasts)
        forecasts.push_back(shortageForecast(row));
    Json decisions = Json::array();
    for (const auto& decision : result.preventiveDecisions) {
        Json row{{"before", shortageForecast(decision.before)}, {"after", shortageForecast(decision.after)},
            {"assignment", decision.assignment ? flightDecision(*decision.assignment) : Json(nullptr)},
            {"mission", nullptr}};
        if (decision.mission) {
            const auto& mission = *decision.mission;
            row["mission"] = {{"id", mission.id}, {"origin", mission.origin}, {"destination", mission.destination},
                {"resource", resourceName(mission.cargoType)}, {"amount", mission.cargoAmount},
                {"deadline", mission.deadline}};
        }
        decisions.push_back(std::move(row));
    }
    return {{"forecasts", forecasts}, {"preventive_decisions", decisions}};
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

Json expandedScore(const CivilizationMetrics& metrics) {
    auto result = score(metrics);
    result.erase("infrastructure");
    result.erase("operating_penalty");
    result["population_coverage_proxy"] = metrics.populationApplicable ? Json(metrics.populationCoverage) : Json(nullptr);
    result["economic_appraisal"] = metrics.developmentApplicable[0] ? Json(metrics.development[0]) : Json(nullptr);
    result["research_appraisal"] = metrics.developmentApplicable[1] ? Json(metrics.development[1]) : Json(nullptr);
    result["colony_appraisal"] = metrics.developmentApplicable[2] ? Json(metrics.development[2]) : Json(nullptr);
    result["mission_loss"] = metrics.missionLoss;
    result["transport_penalty"] = metrics.transportPenalty;
    result["fuel_penalty"] = metrics.fuelPenalty;
    result["construction_penalty"] = metrics.constructionPenalty;
    result["applicable_weight"] = metrics.applicableWeight;
    return result;
}

std::vector<Mission> proposeAid(const benchmark::Scenario& data, const RouteWeights& weights,
    double horizon, Json& proposals, bool experimentalAllocator) {
    std::map<ResourceType, std::vector<ResourceDemand>> byResource;
    for (const auto& demand : data.aidDemands)
        byResource[demand.type].push_back(demand);

    long long nextId = 0;
    for (const auto& mission : data.missions)
        nextId = std::max(nextId, static_cast<long long>(mission.id) + 1);
    for (const auto& event : data.events)
        if (event.urgentMission)
            nextId = std::max(nextId, static_cast<long long>(event.urgentMission->id) + 1);
    std::vector<Mission> aid;
    for (const auto& [type, demands] : byResource) {
        const auto allocation = ResourceAllocator::allocateWithRoutes(demands, data.graph, weights, {}, experimentalAllocator);
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

void writeResult(Input input, std::ostream& destination, PlanningBudget* clock) {
    benchmark::Measurements measurements;
    auto& data = input.data;
    const RouteWeights weights{0.02, 0.03, 0.5, 3.0, 2.0};
    Json proposals = Json::array();
    std::unordered_set<int> colonies;
    for (const auto& colony : data.colonies)
        colonies.insert(colony.id);
    const CivilizationObjective legacy(data.inventory, data.missions, data.projects, colonies,
        input.horizon, input.budget, CivilizationObjective::parseWeights(input.objectiveProfile));
    const auto expanded = legacy.expanded(input.objectiveProfile, data.inventory, data.projects,
        data.populationByLocation(), input.horizon);
    const auto& objective = input.expandedObjective ? expanded : legacy;
    std::unordered_set<int> aidIds;
    AnytimePlan anytime;
    CivilizationPlan plan;
    CoordinatedAid coordinated;
    if (clock) {
        anytime = CivilizationPlanner::anytime(data, legacy, expanded, objective, colonies, input.budget,
            input.horizon, weights, input.knownProductionChanges, *clock, input.feasibilityAwareAllocator,
            true, false, [&] { return proposeAid(data, weights, input.horizon, proposals, input.experimentalAllocator); }, true);
        plan = std::move(anytime.plan);
        coordinated = std::move(anytime.coordinated);
        for (size_t i = data.missions.size(); i < anytime.jobs.size(); ++i)
            aidIds.insert(anytime.jobs[i].id);
        for (auto it = proposals.begin(); it != proposals.end();) {
            if (!aidIds.contains(it->at("mission_id").get<int>()))
                it = proposals.erase(it);
            else
                ++it;
        }
        data.missions = std::move(anytime.jobs);
    } else {
        auto aid = input.feasibilityAwareAllocator ? std::vector<Mission>{} :
            proposeAid(data, weights, input.horizon, proposals, input.experimentalAllocator);
        for (const auto& mission : aid)
            aidIds.insert(mission.id);
        data.missions.insert(data.missions.end(), aid.begin(), aid.end());
        const InventoryPlanningInput inventory{data.inventory, data.aidDemands, aid};
        auto initial = MissionScheduler::schedule(data.missions, data.ships, data.graph, weights, false, {}, &inventory, true);
        plan = CivilizationPlanner::choose(std::move(initial), data.missions, aid, data.inventory,
            data.aidDemands, data.ships, data.graph, data.projects, objective, colonies,
            input.budget, input.horizon, weights, input.knownProductionChanges);
        if (input.feasibilityAwareAllocator) {
            auto known = input.knownProductionChanges;
            const auto builds = CivilizationPlanner::constructionEvents(data.projects, plan.construction);
            known.insert(known.end(), builds.begin(), builds.end());
            coordinated = CivilizationPlanner::coordinateAid(plan, data.missions, data.inventory, data.aidDemands,
                data.ships, data.graph, objective, known, input.horizon,
                LogisticsPlanner::firstAvailableMissionId(data.missions, data.events));
            data.missions.insert(data.missions.end(), coordinated.missions.begin(), coordinated.missions.end());
        }
    }
    for (size_t i = 0; i < coordinated.missions.size(); ++i) {
        const auto& mission = coordinated.missions[i];
        const auto& transfer = coordinated.allocation.transfers[i];
        aidIds.insert(mission.id);
        proposals.push_back({{"mission_id", mission.id}, {"origin", mission.origin},
            {"destination", mission.destination}, {"resource", resourceName(mission.cargoType)},
            {"amount", mission.cargoAmount}, {"transportation_cost", transfer.transportationCost},
            {"route_ids", transfer.routeIds}, {"confirmed", true}});
    }
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
    const auto forecast = clock ? (anytime.forecast ? std::move(*anytime.forecast) : SimulationResult{}) :
        SimulationEngine::run(data.graph, data.inventory, data.missions, data.ships,
            schedule, nominalEvents, {}, false, input.horizon, &nominalEvents);
    const SimulationObjective scoring{objective, construction};
    const auto actual = clock ? (anytime.simulation ? std::move(*anytime.simulation) : SimulationResult{}) :
        SimulationEngine::run(data.graph, data.inventory, data.missions, data.ships,
            schedule, data.events, {}, false, input.horizon, &nominalEvents, &scoring);
    auto realizedLegacy = legacy;
    auto realizedExpanded = expanded;
    for (const auto& request : actual.activatedUrgentMissions) {
        realizedLegacy.addExternalObligation(request);
        realizedExpanded.addExternalObligation(request);
    }
    const bool hasForecast = !clock || anytime.forecast.has_value();
    const bool hasSimulation = !clock || anytime.simulation.has_value();
    const auto nominalLegacy = hasForecast ? score(legacy.evaluate(forecast, construction)) : Json(nullptr);
    const auto nominalExpanded = hasForecast ? expandedScore(expanded.evaluate(forecast, construction)) : Json(nullptr);
    const auto actualLegacy = hasSimulation ? score(realizedLegacy.evaluate(actual, construction)) : Json(nullptr);
    const auto actualExpanded = hasSimulation ? expandedScore(realizedExpanded.evaluate(actual, construction)) : Json(nullptr);

    Json assignments = Json::array();
    for (const auto& row : schedule.scheduled) {
        const auto& mission = *missions.at(row.missionId);
        auto assignment = flightDecision(row);
        assignment.update({{"mission_id", row.missionId}, {"origin", mission.origin},
            {"destination", mission.destination}, {"resource", resourceName(mission.cargoType)},
            {"amount", mission.cargoAmount}});
        assignments.push_back(std::move(assignment));
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
    std::unordered_set<int> urgentIds;
    for (const auto& event : data.events)
        if (event.urgentMission) {
            missions.emplace(event.urgentMission->id, &*event.urgentMission);
            urgentIds.insert(event.urgentMission->id);
        }
    std::unordered_set<int> preventiveIds;
    for (const auto& decision : actual.preventiveDecisions)
        if (decision.mission) {
            missions.emplace(decision.mission->id, &*decision.mission);
            preventiveIds.insert(decision.mission->id);
        }
    for (const auto& [id, mission] : missions) {
        if (!hasSimulation)
            break;
        const char* kind = preventiveIds.contains(id) ? "preventive" : urgentIds.contains(id) ? "urgent" :
            aidIds.contains(id) ? "aid" : "input";
        Json outcome{{"mission_id", id}, {"kind", kind}, {"deadline", mission->deadline}};
        const auto status = actual.missionStatuses.find(id);
        outcome["status"] = status == actual.missionStatuses.end() ? Json(nullptr) : Json(statusName(status->second));
        if (const auto delivery = actual.deliveries.find(id); delivery != actual.deliveries.end()) {
            outcome["delivered_amount"] = delivery->second.amount;
            outcome["arrival"] = delivery->second.arrival;
            outcome["uncertainty"] = flightEstimate(delivery->second.estimate);
        }
        outcomes.push_back(std::move(outcome));
    }
    Json repairs = Json::array();
    for (const auto& record : actual.replanningDecisions)
        repairs.push_back({{"event_id", record.eventId >= 0 ? Json(record.eventId) : Json(nullptr)},
            {"target_id", record.targetId}, {"event_time", record.eventTime},
            {"mission_id", record.missionId}, {"reason", reasonName(record.reason)},
            {"recovered", record.replacement.has_value()},
            {"previous", record.previous ? flightDecision(*record.previous) : Json(nullptr)},
            {"replacement", record.replacement ? flightDecision(*record.replacement) : Json(nullptr)}});
    const Json nominal{
        {"completed", forecast.completed}, {"failed", forecast.failed},
        {"delivered_amount", forecast.deliveredAmount}, {"remaining_shortage", forecast.remainingShortage},
        {"resources", resourceOutcomes(forecast)}, {"shortage_prediction", shortagePrediction(forecast)},
        {"civilization", input.expandedObjective ? nominalExpanded : nominalLegacy},
        {"civilization_legacy", nominalLegacy}, {"civilization_expanded", nominalExpanded}
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
        {"replanning_seconds", actual.replanSeconds}, {"replanning_decisions", repairs},
        {"distance", actual.distance}, {"fuel", actual.fuel}, {"risk", actual.risk}, {"waste", actual.waste},
        {"transportation_cost", actual.transportationCost}, {"delivered_amount", actual.deliveredAmount},
        {"remaining_shortage", actual.remainingShortage}, {"stockout_amount", actual.stockoutAmount},
        {"missions", outcomes}, {"resources", resourceOutcomes(actual)},
        {"shortage_prediction", shortagePrediction(actual)},
        {"civilization", input.expandedObjective ? actualExpanded : actualLegacy},
        {"civilization_legacy", actualLegacy}, {"civilization_expanded", actualExpanded},
        {"service_obligations", realizedLegacy.obligationCount()}
    };
    Json result{
        {"version", 1}, {"horizon", input.horizon}, {"objective_profile", input.objectiveProfile},
        {"objective_model", input.expandedObjective ? "expanded" : "legacy"},
        {"experimental_allocator", input.experimentalAllocator},
        {"feasibility_aware_allocator", input.feasibilityAwareAllocator},
        {"allocation_work", {{"attempts", coordinated.work.attempted}, {"rejected", coordinated.work.rejected},
            {"route_queries", coordinated.work.routeQueries}, {"feasibility_checks", coordinated.work.feasibilityChecks},
            {"partial", coordinated.work.partial}, {"proposed_cargo", coordinated.work.proposedCargo},
            {"trial_committed_cargo", coordinated.work.committedCargo}, {"retained", coordinated.retained}}},
        {"planned", planned}, {"simulation", simulation}, {"processing_seconds", measurements.elapsed()}
    };
    if (clock) {
        Json trace = Json::array();
        std::vector<int> unassigned;
        for (int id : schedule.rejectedMissionIds)
            if (!aidIds.contains(id))
                unassigned.push_back(id);
        for (const auto& row : anytime.trace)
            trace.push_back({{"seconds", row.seconds}, {"legacy_score", row.legacyScore},
                {"expanded_score", row.expandedScore}, {"scheduled", row.scheduled}, {"failed", row.failed},
                {"unscheduled_external", row.unscheduled}, {"timely_service", row.timelyService},
                {"critical_shortage", row.criticalShortage}, {"total_shortage", row.shortage},
                {"unserved_consumption", row.unserved}, {"transportation_cost", row.transportation},
                {"fuel", row.fuel}, {"waste", row.waste}, {"risk", row.risk}});
        result["anytime"] = {{"feasible", anytime.forecast.has_value()}, {"planning_complete", anytime.planningComplete},
            {"simulation_complete", anytime.simulation.has_value()}, {"reason", clock->reason()},
            {"exhaustion_stage", clock->exhaustedAt()},
            {"time_to_first_feasible", anytime.trace.empty() ? Json(nullptr) : Json(anytime.trace.front().seconds)},
            {"time_to_first_nonempty", anytime.firstUseful ? Json(*anytime.firstUseful) : Json(nullptr)},
            {"evaluated", anytime.evaluated}, {"improved", anytime.accepted}, {"interrupted", anytime.interrupted},
            {"validation_seconds", anytime.validationSeconds}, {"reserve_seconds", clock->reserve()},
            {"elapsed_before_serialization_seconds", clock->elapsed()}, {"peak_working_set_bytes", clock->peakBytes()},
            {"remaining_time_seconds", std::isfinite(clock->remaining()) ? Json(clock->remaining()) : Json(nullptr)},
            {"unscheduled_external", unassigned.size()}, {"unscheduled_external_mission_ids", unassigned},
            {"all_external_assigned", hasForecast && unassigned.empty()},
            {"incumbents", std::move(trace)}};
        if (!anytime.forecast)
            result["planned"]["forecast"] = nullptr;
        if (!anytime.simulation)
            result["simulation"] = nullptr;
    }
    destination << std::setw(2) << result << '\n';
}

int runFile(int argc, char* argv[]) {
    if (argc < 5 || std::string(argv[3]) != "--output" || !*argv[2] || !*argv[4])
        throw std::invalid_argument("Usage: --scenario-file INPUT.json --output RESULT.json [--time-budget-ms=N] [--memory-budget-mib=N] [--reserve-ms=N]");
    BudgetLimits limits;
    for (int i = 5; i < argc; ++i)
        if (!limits.parse(argv[i]))
            throw std::invalid_argument("Unknown scenario option: " + std::string(argv[i]));
    PlanningBudget clock(limits);
    const std::filesystem::path inputPath(argv[2]), outputPath(argv[4]);
    std::ifstream source(inputPath);
    if (!source)
        throw std::runtime_error("Cannot open scenario file: " + inputPath.string());
    if (std::filesystem::exists(outputPath) && std::filesystem::equivalent(inputPath, outputPath))
        throw std::invalid_argument("Scenario file and output file must be different.");
    std::ostringstream result;
    writeResult(loadJson(source), result, limits.enabled() ? &clock : nullptr);
    std::ofstream destination(outputPath);
    if (!destination)
        throw std::runtime_error("Cannot open output file: " + outputPath.string());
    destination << result.view();
    destination.close();
    if (!destination)
        throw std::runtime_error("Cannot write output file: " + outputPath.string());
    return 0;
}
}
