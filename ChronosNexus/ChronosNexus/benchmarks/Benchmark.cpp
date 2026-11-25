#include "Benchmark.h"
#include "DatasetGenerator.h"
#include "Measurements.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include "../algorithms/InfrastructureOptimizer.h"
#include "../algorithms/CivilizationPlanner.h"
#include "../algorithms/LogisticsPlanner.h"
#include "../algorithms/PlanImprover.h"
#include "../simulation/SimulationEngine.h"

namespace {
InfrastructureOptimizationResult greedyInfrastructure(std::vector<Infrastructure> projects, int budget) {
    std::sort(projects.begin(), projects.end(), [](const auto& a, const auto& b) {
        const auto av = a.strategicValue / a.constructionCost;
        const auto bv = b.strategicValue / b.constructionCost;
        return av == bv ? a.id < b.id : av > bv;
    });
    InfrastructureOptimizationResult result;
    for (const auto& p : projects) {
        if (result.totalCost + p.constructionCost <= budget) {
            result.totalCost += p.constructionCost;
            result.totalStrategicValue += p.strategicValue;
            result.selectedInfrastructureIds.push_back(p.id);
        }
    }
    return result;
}

void addStressClosures(const SchedulingResult& schedule, std::vector<Event>& events) {
    // Both stress variants use the same pre-disruption schedule and closures.
    std::unordered_set<int> routes;
    for (size_t i = 0; i < schedule.scheduled.size(); i += 8) {
        const auto& entry = schedule.scheduled[i];
        if (entry.routeIds.empty() || !routes.insert(entry.routeIds.front()).second)
            continue;
        Event event;
        event.eventTime = entry.departureTime;
        event.eventType = EventType::RouteBlocked;
        event.affectedObjectId = entry.routeIds.front();
        events.push_back(event);
    }
    std::cout << "Injected " << routes.size() << " exact-edge stress closures at scheduled departure times.\n";
}
}

int runBenchmark(int argc, char* argv[]) {
    if (argc < 4)
        throw std::invalid_argument(
            "Usage: --benchmark SIZE SEED [MODE [--trace-plan] [--objective=PROFILE] [--scenario=CASE] [--experimental-allocator|--feasibility-aware-allocator]]");
    const std::string name = argv[2];
    const auto size = benchmark::dimensions(name);
    size_t consumed = 0;
    const auto seed = std::stoul(argv[3], &consumed);
    if (consumed != std::string(argv[3]).size() || seed > 0xffffffffUL || argv[3][0] == '-')
        throw std::invalid_argument("Seed must be an unsigned 32-bit integer.");
    const std::string mode = argc >= 5 ? argv[4] : "custom";
    const bool improve = mode == "phase2";
    const bool civilization = mode == "phase3";
    const bool inventoryMode = mode == "inventory" || mode == "inventory-uncached" || improve || civilization;
    if (mode != "custom" && mode != "earliest" && mode != "basic" &&
        mode != "all-routes" && mode != "greedy-build" &&
        mode != "stress" && mode != "stress-all-routes" && !inventoryMode)
        throw std::invalid_argument("Unknown benchmark mode.");
    bool trace = false, reportCivilization = civilization;
    bool experimentalAllocator = false;
    bool feasibilityAwareAllocator = false;
    std::string profile = "balanced", scenario = "standard";
    for (int i = 5; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--trace-plan")
            trace = true;
        else if (option == "--experimental-allocator")
            experimentalAllocator = true;
        else if (option == "--feasibility-aware-allocator")
            feasibilityAwareAllocator = true;
        else if (option.starts_with("--objective=")) {
            profile = option.substr(12);
            reportCivilization = true;
        } else if (option.starts_with("--scenario="))
            scenario = option.substr(11);
        else
            throw std::invalid_argument("Unknown benchmark option: " + option);
    }
    if (scenario != "standard" && scenario != "scarcity" && scenario != "production-cut")
        throw std::invalid_argument("Expected standard, scarcity, or production-cut scenario.");
    if (feasibilityAwareAllocator && (!civilization || experimentalAllocator))
        throw std::invalid_argument("Feasibility-aware allocation requires phase3 and a single allocator.");
    const auto coefficients = CivilizationObjective::parseWeights(profile);

    benchmark::Measurements measurements;
    std::cout << "Dataset " << name << ", seed " << seed << ", mode " << mode << ": "
              << size.planets << " planets, " << size.routes << " routes, " << size.missions
              << " input missions\n";
    measurements.report("generation", 0, 0, true);
    auto data = benchmark::generate(size, seed);
    if (scenario != "standard") {
        data.events.clear();
        if (scenario == "scarcity") {
            for (auto& stock : data.inventory)
                stock.available = std::floor(stock.available * 0.4);
            for (auto& demand : data.aidDemands)
                demand.available = std::floor(demand.available * 0.4);
            std::erase_if(data.ships, [](const auto& ship) { return ship.id % 3 != 0; });
        } else {
            for (const auto& donor : data.aidDemands) {
                if (donor.available <= donor.required)
                    continue;
                Event event;
                event.eventType = EventType::ResourceProductionDecrease;
                event.affectedObjectId = donor.planetId;
                event.eventTime = 1;
                event.severity = 1;
                data.events.push_back(event);
            }
        }
    }
    const int budget = std::max(50, size.planets / 8);
    std::unordered_set<int> colonies;
    for (const auto& colony : data.colonies)
        colonies.insert(colony.id);
    const CivilizationObjective objective(data.inventory, data.missions, data.projects,
        colonies, 30, budget, coefficients);
    std::cout << "Population " << data.population << ", colonies " << data.colonies.size()
              << ", stations " << data.stations.size() << ", spacecraft " << data.ships.size()
              << ", aid locations " << data.aidDemands.size() << ", external events "
              << data.events.size() << '\n';
    if (!data.events.empty()) {
        const auto& sample = data.events.front();
        if (scenario == "production-cut")
            std::cout << "Sample disruption: production stops at location " << sample.affectedObjectId
                      << " on day " << sample.eventTime << '\n';
        else
            std::cout << "Sample disruption: route " << sample.affectedObjectId
                      << " closes at day " << sample.eventTime << '\n';
    }

    const RouteWeights weights{0.02, 0.03, 0.5, 3.0, 2.0};
    measurements.report("allocation", 0, 0, true);
    auto allocation = feasibilityAwareAllocator ? AllocationResult{} : mode == "basic" ? ResourceAllocator::allocate(data.aidDemands) :
        ResourceAllocator::allocateWithRoutes(data.aidDemands, data.graph, weights,
            [&](size_t count) { measurements.report("allocation", count, 0); }, experimentalAllocator);
    auto aid = LogisticsPlanner::createMissions(allocation, data.aidDemands,
        ResourceType::Food, 24.0, size.missions);
    if (!aid.empty())
        std::cout << "First aid proposal " << aid.front().origin << " -> " << aid.front().destination
                  << ", cargo " << aid.front().cargoAmount << ", deadline " << aid.front().deadline << '\n';
    data.missions.insert(data.missions.end(), aid.begin(), aid.end());
    std::cout << "Scheduling " << data.missions.size() << " missions, including "
              << aid.size() << " aid proposals.\n";

    measurements.report("scheduling", 0, 0, true);
    const InventoryPlanningInput planning{data.inventory, data.aidDemands, aid, mode != "inventory-uncached"};
    auto schedule = MissionScheduler::schedule(data.missions, data.ships, data.graph,
        weights, mode == "earliest", [&](size_t count) { measurements.report("scheduling", 0, count); },
        inventoryMode ? &planning : nullptr, civilization);
    if (improve) {
        measurements.report("local-search", 0, 0, true);
        schedule = PlanImprover::improve(std::move(schedule), data.missions, data.ships,
            data.graph, data.inventory, weights, 30);
    }
    CivilizationPlan selected;
    if (civilization) {
        measurements.report("civilization-planning", 0, 0, true);
        selected = CivilizationPlanner::choose(std::move(schedule), data.missions, aid,
            data.inventory, data.aidDemands, data.ships, data.graph, data.projects,
            objective, colonies, budget, 30, weights);
        if (feasibilityAwareAllocator) {
            const auto builds = CivilizationPlanner::constructionEvents(data.projects, selected.construction);
            auto coordinated = CivilizationPlanner::coordinateAid(selected, data.missions, data.inventory,
                data.aidDemands, data.ships, data.graph, objective, builds, 30, size.missions);
            aid = std::move(coordinated.missions);
            allocation = std::move(coordinated.allocation);
            data.missions.insert(data.missions.end(), aid.begin(), aid.end());
            const auto& work = coordinated.work;
            std::cout << "COORDINATED_AID," << work.attempted << ',' << work.rejected << ',' << work.partial
                      << ',' << work.routeQueries << ',' << work.feasibilityChecks << ',' << work.proposedCargo
                      << ',' << work.committedCargo << ',' << coordinated.retained << '\n';
        }
        schedule = std::move(selected.schedule);
    }
    // Alternative suppliers change the source, while obligation ID, quantity and deadline stay fixed.
    for (auto& mission : data.missions)
        if (const auto source = schedule.revisedOrigins.find(mission.id);
            source != schedule.revisedOrigins.end())
            mission.origin = source->second;
    if (!schedule.scheduled.empty()) {
        const auto& first = schedule.scheduled.front();
        std::cout << "First assignment: mission " << first.missionId << ", ship " << first.spacecraftId
                  << ", depart " << first.departureTime << ", expected arrival " << first.arrivalTime
                  << ", fuel " << first.fuelUsed << '\n';
    }

    measurements.report("infrastructure", 0, 0, true);
    const auto buildStarted = benchmark::Clock::now();
    const auto construction = civilization ? selected.construction :
        mode == "greedy-build" ? greedyInfrastructure(data.projects, budget) :
        InfrastructureOptimizer::optimize(data.projects, budget);
    const double infrastructureSeconds = std::chrono::duration<double>(benchmark::Clock::now() - buildStarted).count();
    for (int id : construction.selectedInfrastructureIds) {
        const auto& project = data.projects.at(id);
        Event event;
        event.eventType = EventType::ConstructionCompleted;
        event.affectedObjectType = AffectedObjectType::Planet;
        event.affectedObjectId = project.locationId;
        event.eventTime = project.constructionTime;
        event.severity = project.resourceImpact.at(ResourceType::Food);
        data.events.push_back(event);
    }
    if (mode == "stress" || mode == "stress-all-routes")
        addStressClosures(schedule, data.events);
    measurements.report("simulation", 0, 0, true);
    const SimulationObjective scoring{objective, construction};
    const auto actual = SimulationEngine::run(data.graph, data.inventory, data.missions, data.ships,
        schedule, data.events, [&](size_t count) { measurements.report("simulation", 0, count); },
        mode == "all-routes" || mode == "stress-all-routes", 30, nullptr, &scoring);
    measurements.report("complete", 0, actual.completed + actual.failed, true);
    benchmark::reportResults(data.missions, size.missions, aid.size(), allocation, schedule, construction,
        actual, infrastructureSeconds, measurements, inventoryMode, improve, trace);
    std::unordered_map<int, double> pendingAid;
    for (const auto& job : aid)
        pendingAid.emplace(job.id, job.cargoAmount);
    double scheduledAidCargo = 0;
    size_t scheduledAid = 0;
    for (const auto& row : schedule.scheduled)
        if (const auto job = pendingAid.find(row.missionId); job != pendingAid.end()) {
            scheduledAidCargo += job->second;
            ++scheduledAid;
        }
    std::cout << "AID_CARGO," << scheduledAidCargo << ',' << aid.size() - scheduledAid << '\n';
    if (trace) {
        for (const auto& job : aid) {
            const auto delivery = actual.deliveries.find(job.id);
            std::cout << "AID_TRACE," << job.id << ',' << job.origin << ',' << job.destination << ','
                      << job.cargoAmount << ',' << job.deadline << ','
                      << (delivery == actual.deliveries.end() ? 0 : delivery->second.amount) << '\n';
        }
    }
    if (reportCivilization) {
        auto realizedObjective = objective;
        for (const auto& request : actual.activatedUrgentMissions)
            realizedObjective.addExternalObligation(request);
        const auto m = realizedObjective.evaluate(actual, construction);
        std::cout << std::setprecision(12) << "CIVILIZATION,"
            << (profile.find(',') == std::string::npos ? profile : "custom-weights") << ',' << scenario << ','
            << m.score << ',' << m.security << ',' << m.criticalSecurity << ',' << m.service << ','
            << m.infrastructure << ',' << m.operatingPenalty << ',' << m.wastePenalty << ','
            << m.riskPenalty << ',' << m.criticalEndingShortage << ',' << m.criticalUnserved << ','
            << selected.evaluated << ',' << selected.feasible << ',' << selected.forecastScore << '\n';
    }
    return 0;
}
