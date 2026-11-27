#include "CivilizationPlanner.h"
#include "PlanImprover.h"
#include "LogisticsPlanner.h"
#include "../benchmarks/DatasetGenerator.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <map>
#include <set>
#include <tuple>
#include <random>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <deque>
#include <numeric>
#include <sstream>
#include <cmath>
#include <bit>
#include "../third_party/nlohmann/json.hpp"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
using Json = nlohmann::json;

Json bookingRows(const SchedulingResult& schedule) {
    Json rows = Json::array();
    for (const auto& s : schedule.scheduled)
        rows.push_back(Json::array({s.missionId, s.spacecraftId, s.departureTime, s.arrivalTime, s.fuelUsed,
            s.path, s.routeIds, {s.weights.distance, s.weights.fuel, s.weights.time, s.weights.risk, s.weights.reliability},
            {s.estimate.earliestArrival, s.estimate.expectedArrival, s.estimate.latestArrival,
                s.estimate.routeReliability, s.estimate.risk}}));
    return rows;
}

Json missionRows(const std::vector<Mission>& jobs) {
    Json rows = Json::array();
    for (const auto& m : jobs)
        rows.push_back(Json::array({m.id, m.origin, m.destination, static_cast<int>(m.cargoType), m.cargoAmount,
            m.deadline, m.priority, m.importance, m.requirements.minCargoCapacity, m.requirements.minFuelCapacity,
            m.requirements.minSpeed, m.requirements.minReliability}));
    return rows;
}

Mission readMission(const Json& row) {
    Mission m;
    m.id = row.at(0); m.origin = row.at(1); m.destination = row.at(2);
    const int type = row.at(3);
    if (type < 0 || type > static_cast<int>(ResourceType::ConstructionMaterials))
        throw std::runtime_error("Invalid checkpoint resource.");
    m.cargoType = static_cast<ResourceType>(type); m.cargoAmount = row.at(4);
    m.deadline = row.at(5); m.priority = row.at(6); m.importance = row.at(7);
    m.requirements = {row.at(8), row.at(9), row.at(10), row.at(11)};
    return m;
}

ScheduledMission readBooking(const Json& row) {
    const auto& w = row.at(7);
    const auto& f = row.at(8);
    return {row.at(0), row.at(1), row.at(2), row.at(3), row.at(4), row.at(5), row.at(6),
        {w.at(0), w.at(1), w.at(2), w.at(3), w.at(4)},
        {f.at(0), f.at(1), f.at(2), f.at(3), f.at(4)}};
}

void replaceCheckpoint(const std::string& path, const Json& state) {
    const std::filesystem::path target(path);
    auto temporary = target;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << state;
        output.close();
        if (!output)
            throw std::runtime_error("Cannot write optimization checkpoint.");
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish optimization checkpoint.");
#else
    std::filesystem::rename(temporary, target);
#endif
}

double overall(const LongSearchOptions& config, const CivilizationMetrics& old,
    const CivilizationMetrics& expanded, double seconds, size_t bytes) {
    const double quality = (old.security + old.criticalSecurity + expanded.service + old.infrastructure) / 4;
    const double burden = (expanded.transportPenalty + expanded.fuelPenalty + expanded.wastePenalty +
        expanded.riskPenalty + expanded.constructionPenalty) / 5;
    const auto& w = config.coefficients;
    return w[0] * quality - w[1] * seconds / config.timeReference -
        w[2] * static_cast<double>(bytes) / (1048576 * config.memoryReferenceMiB) - w[3] * burden;
}

struct ValidationTimer {
    double* total;
    PlanningBudget::Clock::time_point start = PlanningBudget::Clock::now();
    ~ValidationTimer() {
        if (total)
            *total += std::chrono::duration<double>(PlanningBudget::Clock::now() - start).count();
    }
};

std::vector<Mission> withOrigins(std::vector<Mission> jobs, const SchedulingResult& schedule) {
    for (auto& mission : jobs)
        if (const auto src = schedule.revisedOrigins.find(mission.id); src != schedule.revisedOrigins.end())
            mission.origin = src->second;
    return jobs;
}

bool intact(const SchedulingResult& schedule, const std::vector<Mission>& jobs, const SimulationResult& replay) {
    return replay.completed == schedule.missionsScheduled &&
        std::none_of(jobs.begin(), jobs.end(), [&](const Mission& job) {
            const auto delivery = replay.deliveries.find(job.id);
            return delivery != replay.deliveries.end() &&
                (delivery->second.arrival > job.deadline || delivery->second.amount + 1e-8 < job.cargoAmount);
        });
}
}

std::vector<Event> CivilizationPlanner::constructionEvents(const std::vector<Infrastructure>& projects,
    const InfrastructureOptimizationResult& construction) {
    std::vector<Event> result;
    for (int id : construction.selectedInfrastructureIds) {
        const auto project = std::find_if(projects.begin(), projects.end(),
            [=](const Infrastructure& p) { return p.id == id; });
        if (project == projects.end())
            continue;
        // The simulator currently interprets construction as a food production change.
        const auto impact = project->resourceImpact.find(ResourceType::Food);
        if (impact == project->resourceImpact.end())
            continue;
        Event e;
        e.eventType = EventType::ConstructionCompleted;
        e.affectedObjectType = AffectedObjectType::Planet;
        e.affectedObjectId = project->locationId;
        const auto start = construction.startDays.find(id);
        e.eventTime = project->constructionTime + (start == construction.startDays.end() ? 0 : start->second);
        e.severity = impact->second;
        result.push_back(e);
    }
    return result;
}

CivilizationPlan CivilizationPlanner::choose(SchedulingResult initial, const std::vector<Mission>& obligations,
    const std::vector<Mission>& aid, const std::vector<ResourceDemand>& inventory,
    const std::vector<ResourceDemand>& aidNetwork, const std::vector<Spacecraft>& ships,
    const GalaxyGraph& graph, const std::vector<Infrastructure>& projects,
    const CivilizationObjective& objective, const std::unordered_set<int>& colonies,
    int budget, double horizon, const RouteWeights& weights,
    const std::vector<Event>& knownProductionChanges, const CandidateObserver& observer,
    double* validationSeconds, int* evaluatedCandidates) {
    CivilizationPlan best;
    int evaluated = 0, feasible = 0;
    auto nominalEvents = [&](const InfrastructureOptimizationResult& build) {
        auto construction = constructionEvents(projects, build);
        if (knownProductionChanges.empty())
            return construction;
        std::vector<Event> events;
        // Callers supply only changes already announced at planning time.
        for (const auto& change : knownProductionChanges)
            if (change.eventType == EventType::ResourceProductionDecrease)
                events.push_back(change);
        events.insert(events.end(), construction.begin(), construction.end());
        return events;
    };
    auto consider = [&](const SchedulingResult& schedule, const InfrastructureOptimizationResult& construction) {
        ++evaluated;
        if (evaluatedCandidates)
            ++*evaluatedCandidates;
        ValidationTimer timer{validationSeconds};
        const auto missions = withOrigins(obligations, schedule);
        auto forecast = SimulationEngine::run(graph, inventory, missions, ships, schedule,
            nominalEvents(construction), {}, false, horizon);
        if (!intact(schedule, missions, forecast))
            return;
        ++feasible;
        const auto metrics = objective.evaluate(forecast, construction);
        if (feasible == 1 || objective.better(metrics, best.forecastMetrics)) {
            best.schedule = schedule;
            best.construction = construction;
            best.forecastScore = metrics.score;
            best.forecastMetrics = metrics;
        }
        if (observer)
            observer(schedule, construction, std::move(forecast));
    };

    const auto standard = InfrastructureOptimizer::optimize(projects, budget);
    consider(initial, standard);
    consider(initial, {});
    if (objective.usesExpanded()) {
        auto appraised = projects;
        for (auto& project : appraised)
            project.strategicValue = objective.developmentPreference(project);
        auto development = InfrastructureOptimizer::optimize(appraised, budget);
        const std::unordered_set<int> selected(development.selectedInfrastructureIds.begin(),
            development.selectedInfrastructureIds.end());
        development.totalStrategicValue = 0;
        for (const auto& project : projects)
            if (selected.contains(project.id))
                development.totalStrategicValue += project.strategicValue;
        if (!selected.empty() && development.selectedInfrastructureIds != standard.selectedInfrastructureIds)
            consider(initial, development);
    }
    if (std::any_of(obligations.begin(), obligations.end(), [](const Mission& mission) { return mission.importance > 0; })) {
        const InventoryPlanningInput priorityInventory{inventory, aidNetwork, aid};
        auto priorityPlan = MissionScheduler::schedule(obligations, ships, graph, weights, false, {}, &priorityInventory);
        consider(priorityPlan, standard);
    }
    const auto local = PlanImprover::improve(initial, obligations, ships, graph, inventory, weights, horizon, true);
    consider(local, standard);

    std::map<std::pair<int, ResourceType>, const ResourceDemand*> balances;
    for (const auto& b : inventory)
        balances.emplace(std::make_pair(b.planetId, b.type), &b);

    // Revalue production where the unassisted forecast shows a deficit.
    auto targeted = projects;
    for (auto& project : targeted) {
        project.strategicValue = objective.developmentPreference(project);
        const auto b = balances.find({project.locationId, ResourceType::Food});
        const auto impact = project.resourceImpact.find(ResourceType::Food);
        if (b == balances.end() || impact == project.resourceImpact.end())
            continue;
        const auto& stock = *b->second;
        const double deficit = std::max(0.0, stock.required - stock.available +
            (stock.consumption - stock.production) * horizon);
        const double benefit = std::min(deficit, std::max(0.0, horizon - project.constructionTime) *
            std::max(0.0, impact->second));
        project.strategicValue += benefit * (colonies.contains(project.locationId) ? 2 : 1);
    }
    auto securityBuild = InfrastructureOptimizer::optimize(targeted, budget);
    securityBuild.totalStrategicValue = 0;
    for (const auto& p : projects)
        if (std::find(securityBuild.selectedInfrastructureIds.begin(),
                securityBuild.selectedInfrastructureIds.end(), p.id) != securityBuild.selectedInfrastructureIds.end())
            securityBuild.totalStrategicValue += p.strategicValue;

    auto securedSchedule = [&](const InfrastructureOptimizationResult& build, bool preferEarliest) {
        std::map<std::pair<int, ResourceType>, double> production;
        for (const auto& e : constructionEvents(projects, build))
            production[{e.affectedObjectId, e.resourceType}] +=
                std::max(0.0, horizon - e.eventTime) * std::max(0.0, e.severity);
        auto jobs = obligations;
        std::unordered_set<int> aidIds;
        for (const auto& m : aid)
            aidIds.insert(m.id);
        for (auto& m : jobs) {
            const auto key = std::make_pair(m.destination, m.cargoType);
            if (const auto b = balances.find(key); b != balances.end()) {
                const auto& stock = *b->second;
                const double forecast = stock.available + horizon * (stock.production - stock.consumption) -
                    stock.required + production[key];
                if (forecast < 0)
                    m.priority += colonies.contains(m.destination) ? 8 : 3;
                else if (aidIds.contains(m.id))
                    m.priority -= 4; // Keep the obligation, but defer noncritical aid in this candidate.
            }
        }

        // Compare a bounded number of alternate donors with locally available ships.
        std::unordered_map<int, std::vector<const Spacecraft*>> localShips;
        for (const auto& ship : ships)
            localShips[ship.currentLocation].push_back(&ship);
        std::set<std::pair<int, ResourceType>> donors;
        for (const auto& stock : aidNetwork)
            if (stock.available > stock.required)
                donors.emplace(stock.planetId, stock.type);
        std::vector<Mission*> proposals;
        for (auto& m : jobs)
            if (aidIds.contains(m.id))
                proposals.push_back(&m);
        std::sort(proposals.begin(), proposals.end(), [](const Mission* a, const Mission* b) {
            return std::tuple{-a->priority, a->deadline, a->id} < std::tuple{-b->priority, b->deadline, b->id};
        });
        if (proposals.size() > 24)
            proposals.resize(24);
        std::unordered_map<int, int> revised;
        if (!proposals.empty()) {
            const auto reverse = graph.reversed();
            for (auto* m : proposals) {
                const auto path = Dijkstra::findNearest(reverse, m->destination, [&](int id) {
                    if (id == m->destination || !donors.contains({id, m->cargoType}) ||
                        !localShips.contains(id))
                        return false;
                    const auto& stock = *balances.at({id, m->cargoType});
                    return stock.available - stock.required >= m->cargoAmount &&
                        std::any_of(localShips.at(id).begin(), localShips.at(id).end(),
                            [&](const Spacecraft* ship) { return ship->maxCargoCapacity >= m->cargoAmount; });
                }, weights, m->cargoAmount);
                if (path.found && path.path.back() != m->origin) {
                    m->origin = path.path.back();
                    revised.emplace(m->id, m->origin);
                }
            }
        }
        const InventoryPlanningInput planning{inventory, aidNetwork, aid};
        auto schedule = MissionScheduler::schedule(jobs, ships, graph, weights, preferEarliest, {}, &planning, true);
        for (const auto& entry : schedule.scheduled)
            if (const auto src = revised.find(entry.missionId); src != revised.end())
                schedule.revisedOrigins.try_emplace(entry.missionId, src->second);
        return schedule;
    };
    consider(securedSchedule(standard, false), standard);
    consider(securedSchedule(securityBuild, true), securityBuild);

    if (feasible == 0 || objective.prioritizesSecurity()) {
        SchedulingResult empty;
        for (const auto& m : obligations)
            empty.rejectedMissionIds.push_back(m.id);
        consider(empty, {});
    }
    best.evaluated = evaluated;
    best.feasible = feasible;
    return best;
}
CoordinatedAid CivilizationPlanner::coordinateAid(CivilizationPlan& plan, const std::vector<Mission>& obligations,
    const std::vector<ResourceDemand>& accounts, const std::vector<ResourceDemand>& network,
    const std::vector<Spacecraft>& ships, const GalaxyGraph& graph, const CivilizationObjective& objective,
    const std::vector<Event>& knownSupply, double horizon, int firstAidId, bool indexedAid,
    const AidRoutingOptions& aidRouting) {
    CoordinatedAid aid;
    if (network.empty())
        return aid;
    const SimulationObjective scoring{objective, plan.construction};
    auto started = std::chrono::steady_clock::now();
    const auto trial = SimulationEngine::run(graph, accounts, obligations, ships, plan.schedule,
        {}, {}, false, horizon, &knownSupply, &scoring, &network, indexedAid, aidRouting);
    aid.coordinationSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    aid.work = trial.allocationWork;
    aid.allocation.nodesExplored = trial.nodesExplored;
    aid.allocation.candidates = trial.allocationWork.routeQueries;
    auto candidate = plan.schedule;
    auto missions = obligations;
    for (const auto& proposal : trial.preventiveDecisions) {
        if (!proposal.assignment || !proposal.mission)
            continue;
        if (firstAidId == std::numeric_limits<int>::max())
            throw std::invalid_argument("Not enough mission IDs for coordinated aid.");
        auto assignment = *proposal.assignment;
        auto mission = *proposal.mission;
        assignment.missionId = mission.id = firstAidId++;
        missions.push_back(mission);
        aid.missions.push_back(mission);
        candidate.scheduled.push_back(assignment);
        ++candidate.missionsScheduled;
        double routeCost = 0;
        for (int edge : assignment.routeIds)
            routeCost += Dijkstra::edgeCost(*graph.findRoute(edge), assignment.weights);
        aid.allocation.transfers.push_back({mission.origin, mission.destination, mission.cargoAmount,
            routeCost * mission.cargoAmount, assignment.path, assignment.routeIds});
        aid.allocation.totalDelivered += mission.cargoAmount;
    }
    if (aid.missions.empty())
        return aid;
    ++plan.evaluated;
    started = std::chrono::steady_clock::now();
    const auto forecast = SimulationEngine::run(graph, accounts, missions, ships, candidate,
        knownSupply, {}, false, horizon);
    aid.replaySeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::unordered_map<int, const Mission*> byId;
    for (const auto& job : missions)
        byId.emplace(job.id, &job);
    const bool fullDeliveries = std::all_of(candidate.scheduled.begin(), candidate.scheduled.end(), [&](const auto& row) {
        const auto delivery = forecast.deliveries.find(row.missionId);
        if (delivery == forecast.deliveries.end())
            return false;
        const auto& job = *byId.at(row.missionId);
        return delivery->second.arrival <= job.deadline && delivery->second.amount + 1e-8 >= job.cargoAmount;
    });
    if (fullDeliveries) {
        ++plan.feasible;
        const auto metrics = objective.evaluate(forecast, plan.construction);
        auto incumbent = plan.forecastMetrics;
        incumbent.score = plan.forecastScore;
        if (objective.better(metrics, incumbent)) {
            plan.schedule = std::move(candidate);
            plan.forecastScore = metrics.score;
            plan.forecastMetrics = metrics;
            aid.retained = true;
            return aid;
        }
    }
    aid.missions.clear();
    aid.allocation.transfers.clear();
    aid.allocation.totalDelivered = 0;
    return aid;
}

AnytimePlan CivilizationPlanner::anytime(const benchmark::Scenario& data, const CivilizationObjective& legacy,
    const CivilizationObjective& expanded, const CivilizationObjective& objective,
    const std::unordered_set<int>& colonies, int budget, double horizon, const RouteWeights& weights,
    const std::vector<Event>& knownProductionChanges, PlanningBudget& clock,
    bool coordinatedAllocator, bool indexedAid, bool reverseBounds,
    const std::function<std::vector<Mission>()>& proposeAid, bool proactiveSimulation,
    std::optional<int> profileInitial, const LongSearchOptions* longSearch) {
    AnytimePlan result;
    result.initialProfile.useWorkspace = profileInitial.value_or(0) > 0;
    result.initialProfile.pruneRoutes = profileInitial.value_or(0) > 1;
    result.jobs = data.missions;
    auto publish = [&](const SchedulingResult& schedule, const InfrastructureOptimizationResult& construction,
        SimulationResult&& replay, const std::vector<Mission>& jobs) {
        const auto active = objective.evaluate(replay, construction);
        const double ready = clock.elapsed();
        if (schedule.missionsScheduled > 0 && !result.firstUseful)
            result.firstUseful = ready;
        if (result.forecast && !objective.better(active, result.plan.forecastMetrics))
            return;

        const auto old = legacy.evaluate(replay, construction);
        const auto next = expanded.evaluate(replay, construction);
        auto committed = withOrigins(jobs, schedule);
        auto accepted = schedule;
        std::unordered_set<int> scheduled;
        for (const auto& row : schedule.scheduled)
            scheduled.insert(row.missionId);
        accepted.rejectedMissionIds.clear();
        int unscheduled = 0;
        for (const auto& job : committed)
            if (!scheduled.contains(job.id))
                accepted.rejectedMissionIds.push_back(job.id);
        for (const auto& job : data.missions)
            unscheduled += !scheduled.contains(job.id);
        PlanningBudget::checkpoint();
        result.plan.schedule = std::move(accepted);
        result.plan.construction = construction;
        result.plan.forecastScore = active.score;
        result.plan.forecastMetrics = active;
        result.jobs = std::move(committed);
        result.trace.push_back({clock.elapsed(), old.score, next.score, active.service,
            active.criticalEndingShortage, replay.remainingShortage, replay.stockoutAmount,
            replay.transportationCost, replay.fuel, replay.waste, replay.risk,
            replay.completed, replay.failed, unscheduled, next.populationCoverage, old.criticalUnserved});
        result.forecast = std::move(replay);
        if (result.trace.size() > 1)
            ++result.accepted;
    };
    auto replay = [&](const SchedulingResult& schedule, const InfrastructureOptimizationResult& construction,
        const std::vector<Mission>& jobs, CivilizationMetrics* evaluatedMetrics = nullptr) {
        PlanningBudget::checkpoint();
        ++result.evaluated;
        ValidationTimer timer{&result.validationSeconds};
        auto events = knownProductionChanges;
        const auto builds = constructionEvents(data.projects, construction);
        events.insert(events.end(), builds.begin(), builds.end());
        const auto revised = withOrigins(jobs, schedule);
        auto forecast = SimulationEngine::run(data.graph, data.inventory, revised, data.ships, schedule,
            events, {}, false, horizon);
        const bool valid = intact(schedule, revised, forecast) && (!longSearch || !forecast.rerouted) &&
            (!longSearch || !longSearch->neighborhoods ||
                std::all_of(schedule.scheduled.begin(), schedule.scheduled.end(), [&](const auto& leg) {
                    const auto delivery = forecast.deliveries.find(leg.missionId);
                    return delivery != forecast.deliveries.end() &&
                        std::abs(delivery->second.arrival - leg.arrivalTime) <= 1e-8;
                }));
        if (valid && evaluatedMetrics)
            *evaluatedMetrics = objective.evaluate(forecast, construction);
        if (valid)
            publish(schedule, construction, std::move(forecast), jobs);
        return valid;
    };

    std::mt19937 random(longSearch ? longSearch->seed : 0);
    std::array<size_t, 3> trials{}, wins{};
    std::array<double, 3> costs{};
    size_t iterations = 0, duplicates = 0, feasible = 0, evaluated = 0, improvements = 0;
    Json pool = Json::array(), frontier;
    std::unordered_set<std::string> columnKeys;
    size_t poolBytes = 0, nextColumn = 0, partner = 0, masterTrials = 0, masterWins = 0;
    size_t generated = 0, repeated = 0, capacityRejected = 0, compatibilityRejected = 0, throttled = 0;
    const auto columnKey = [](Json column) {
        for (auto& booking : column.at("bookings"))
            booking.erase(booking.begin() + 7); // Nominal routing strategy is irrelevant after the route is fixed.
        return column.dump();
    };
    double elapsedBefore = 0, lastReport = 0, peakOverall = -std::numeric_limits<double>::infinity();
    std::ofstream progress;
    if (longSearch && !longSearch->progress.empty()) {
        const bool append = longSearch->resume && std::filesystem::exists(longSearch->progress);
        progress.open(longSearch->progress, append ? std::ios::app : std::ios::trunc);
        if (!progress)
            throw std::runtime_error("Cannot open optimization progress file.");
        if (!append)
            progress << "seconds,remaining_seconds,iterations,duplicates,feasible,improvements,operator,legacy,expanded,overall,best_observed_overall,assigned,coverage,critical_ending,total_ending,unserved,service,transport,fuel,waste,risk,projects,construction,peak_bytes,last_improvement_seconds,checkpoint,evaluated,rejected,multi_start_trials,portfolio_trials,rebuild_trials,multi_start_wins,portfolio_wins,rebuild_wins,best_found_seconds\n";
    }
    const auto portfolio = [&](const std::vector<int>& ids) {
        InfrastructureOptimizationResult build;
        std::unordered_set<int> seen;
        for (int id : ids) {
            const auto p = std::find_if(data.projects.begin(), data.projects.end(),
                [=](const Infrastructure& project) { return project.id == id; });
            if (p == data.projects.end() || !seen.insert(id).second ||
                p->status != ConstructionStatus::Proposed || !std::isfinite(p->constructionCost) ||
                p->constructionCost < 0)
                throw std::runtime_error("Invalid project in long-search portfolio.");
            build.totalCost += p->constructionCost;
            build.totalStrategicValue += p->strategicValue;
            build.selectedInfrastructureIds.push_back(id);
        }
        return build;
    };
    const auto readPortfolio = [&](const Json& ids, const Json& starts) {
        auto build = portfolio(ids.get<std::vector<int>>());
        build.startDays = starts.get<std::unordered_map<int, double>>();
        if (build.totalCost > budget)
            throw std::runtime_error("Checkpoint exceeds construction budget.");
        for (const auto& [id, day] : build.startDays) {
            const auto project = std::find_if(data.projects.begin(), data.projects.end(),
                [=](const auto& row) { return row.id == id; });
            if (std::find(build.selectedInfrastructureIds.begin(), build.selectedInfrastructureIds.end(), id) ==
                    build.selectedInfrastructureIds.end() || project == data.projects.end() ||
                !std::isfinite(day) || day < 0 || day + project->constructionTime > horizon)
                throw std::runtime_error("Invalid checkpoint construction timing.");
        }
        return build;
    };
    const auto addColumns = [&](const SchedulingResult& schedule, const std::vector<Mission>& jobs) {
        std::map<int, SchedulingResult> chains;
        std::unordered_map<int, const Mission*> byId;
        for (const auto& job : jobs)
            byId.emplace(job.id, &job);
        for (const auto& leg : schedule.scheduled)
            chains[leg.spacecraftId].scheduled.push_back(leg);
        for (const auto& [ship, chain] : chains) {
            ++generated;
            std::vector<Mission> manifests;
            for (const auto& leg : chain.scheduled)
                manifests.push_back(*byId.at(leg.missionId));
            Json column{{"ship", ship}, {"bookings", bookingRows(chain)}, {"jobs", missionRows(manifests)}};
            auto key = columnKey(column);
            if (columnKeys.contains(key))
                ++repeated;
            else if (poolBytes + key.size() > 8 * 1024 * 1024)
                ++capacityRejected;
            else {
                columnKeys.insert(key);
                poolBytes += key.size();
                pool.push_back(std::move(column));
            }
        }
    };
    auto report = [&](const char* strategy, bool force) {
        const double elapsed = clock.elapsed();
        if (!longSearch || !result.forecast || (!force && elapsed - lastReport < longSearch->reportSeconds))
            return;
        lastReport = elapsed;
        const auto& sample = result.trace.back();
        const auto a = legacy.evaluate(*result.forecast, result.plan.construction);
        const auto b = expanded.evaluate(*result.forecast, result.plan.construction);
        const auto peak = clock.peakBytes();
        const double score = overall(*longSearch, a, b, elapsed + elapsedBefore, peak);
        peakOverall = std::max(peakOverall, score);
        if (!longSearch->checkpoint.empty() && !longSearch->replayOnly) {
            std::ostringstream rng;
            rng << random;
            replaceCheckpoint(longSearch->checkpoint, {{"version", 1}, {"identity", longSearch->identity},
                {"elapsed", elapsed + elapsedBefore}, {"remaining", clock.remaining()}, {"rng", rng.str()},
                {"iterations", iterations}, {"duplicates", duplicates}, {"feasible", feasible}, {"trials", trials},
                {"evaluated", evaluated}, {"improvements", improvements},
                {"wins", wins}, {"costs", costs}, {"best_overall", peakOverall},
                {"jobs", missionRows(result.jobs)}, {"bookings", bookingRows(result.plan.schedule)},
                {"projects", result.plan.construction.selectedInfrastructureIds}, {"legacy_score", a.score},
                {"expanded_score", b.score}, {"start_days", result.plan.construction.startDays},
                {"pool", pool}, {"frontier", frontier}, {"next_column", nextColumn}, {"partner", partner},
                {"master_trials", masterTrials}, {"master_wins", masterWins},
                {"generated", generated}, {"repeated", repeated}, {"capacity_rejected", capacityRejected},
                {"compatibility_rejected", compatibilityRejected}, {"throttled", throttled}});
        }
        std::cout << std::setprecision(10) << "LONG_SEARCH," << elapsed + elapsedBefore << ',' << clock.remaining()
                  << ',' << iterations << ',' << duplicates << ',' << feasible << ',' << strategy
                  << ',' << result.plan.forecastScore << ',' << score << ',' << sample.scheduled << ','
                  << sample.essentialCoverage << ',' << sample.criticalShortage << ',' << peak << std::endl;
        if (longSearch->expandingHybrid)
            std::cout << "EXPANDING_MASTER," << elapsed + elapsedBefore << ',' << pool.size() << ','
                      << poolBytes << ',' << nextColumn << ',' << partner << ',' << masterTrials << ','
                      << masterWins << ',' << result.plan.forecastScore << ',' << generated << ',' << repeated
                      << ',' << capacityRejected << ',' << compatibilityRejected << ',' << throttled << std::endl;
        if (progress.is_open()) {
            progress << std::setprecision(15) << elapsed + elapsedBefore << ',' << clock.remaining() << ','
                << iterations << ',' << duplicates << ',' << feasible << ',' << improvements << ',' << strategy
                << ',' << a.score << ',' << b.score << ',' << score << ',' << peakOverall << ','
                << data.missions.size() - sample.unscheduled << ',' << sample.essentialCoverage << ','
                << sample.criticalShortage << ',' << sample.shortage << ',' << sample.unserved << ','
                << sample.timelyService << ',' << sample.transportation << ',' << sample.fuel << ',' << sample.waste
                << ',' << sample.risk << ',' << result.plan.construction.selectedInfrastructureIds.size() << ','
                << result.plan.construction.totalCost << ',' << peak << ',' << elapsed - sample.seconds << ','
                << !longSearch->checkpoint.empty() << ',' << evaluated << ',' << evaluated - feasible;
            for (auto total : trials)
                progress << ',' << total;
            for (auto total : wins)
                progress << ',' << total;
            progress << ',' << elapsedBefore + sample.seconds << std::endl;
        }
    };
    auto restore = [&] {
        std::ifstream file(longSearch->checkpoint, std::ios::binary);
        if (!file)
            throw std::runtime_error("Cannot open optimization checkpoint.");
        Json state;
        file >> state;
        if (state.at("version") != 1 || state.at("identity") != longSearch->identity)
            throw std::runtime_error("Optimization checkpoint is incompatible with this run.");
        std::vector<Mission> jobs;
        for (const auto& row : state.at("jobs")) {
            Mission m;
            m.id = row.at(0); m.origin = row.at(1); m.destination = row.at(2);
            const int type = row.at(3);
            if (type < 0 || type > static_cast<int>(ResourceType::ConstructionMaterials))
                throw std::runtime_error("Invalid checkpoint resource.");
            m.cargoType = static_cast<ResourceType>(type); m.cargoAmount = row.at(4);
            m.deadline = row.at(5); m.priority = row.at(6); m.importance = row.at(7);
            m.requirements = {row.at(8), row.at(9), row.at(10), row.at(11)};
            jobs.push_back(m);
        }
        std::unordered_map<int, size_t> ids;
        for (size_t i = 0; i < jobs.size(); ++i)
            if (!ids.emplace(jobs[i].id, i).second)
                throw std::runtime_error("Duplicate checkpoint mission.");
        for (const auto& original : data.missions) {
            const auto& restored = jobs.at(ids.at(original.id));
            if (missionRows({restored}) != missionRows({original}))
                throw std::runtime_error("Checkpoint changed an external obligation.");
        }
        SchedulingResult schedule;
        std::unordered_set<int> assigned;
        for (const auto& row : state.at("bookings")) {
            const int id = row.at(0);
            if (!ids.contains(id) || !assigned.insert(id).second)
                throw std::runtime_error("Invalid checkpoint assignment ID.");
            const auto& w = row.at(7);
            const auto& f = row.at(8);
            schedule.scheduled.push_back({id, row.at(1), row.at(2), row.at(3), row.at(4), row.at(5), row.at(6),
                {w.at(0), w.at(1), w.at(2), w.at(3), w.at(4)},
                {f.at(0), f.at(1), f.at(2), f.at(3), f.at(4)}});
        }
        schedule.missionsScheduled = static_cast<int>(schedule.scheduled.size());
        auto construction = portfolio(state.at("projects").get<std::vector<int>>());
        if (longSearch->expandingHybrid) {
            construction = readPortfolio(state.at("projects"), state.at("start_days"));
            pool = state.at("pool");
            frontier = state.at("frontier");
            nextColumn = state.at("next_column"); partner = state.at("partner");
            masterTrials = state.at("master_trials"); masterWins = state.at("master_wins");
            generated = state.at("generated"); repeated = state.at("repeated");
            capacityRejected = state.at("capacity_rejected");
            compatibilityRejected = state.at("compatibility_rejected"); throttled = state.at("throttled");
            if (!pool.is_array() || nextColumn > pool.size() || partner > std::bit_width(nextColumn) ||
                (nextColumn == pool.size() && (!frontier.is_null() || partner)))
                throw std::runtime_error("Invalid expanding master checkpoint cursor.");
            for (const auto& col : pool) {
                auto key = columnKey(col);
                poolBytes += key.size();
                if (poolBytes > 8 * 1024 * 1024 || !columnKeys.insert(std::move(key)).second)
                    throw std::runtime_error("Invalid expanding master checkpoint pool.");
            }
        }
        if (construction.totalCost > budget)
            throw std::runtime_error("Checkpoint exceeds construction budget.");
        elapsedBefore = state.at("elapsed");
        iterations = state.at("iterations"); duplicates = state.at("duplicates"); feasible = state.at("feasible");
        evaluated = state.at("evaluated"); improvements = state.at("improvements");
        if (feasible > evaluated || improvements > feasible)
            throw std::runtime_error("Invalid checkpoint evaluation counts.");
        trials = state.at("trials"); wins = state.at("wins"); costs = state.at("costs");
        peakOverall = state.at("best_overall");
        std::istringstream rng(state.at("rng").get<std::string>());
        if (!(rng >> random) || elapsedBefore < 0 || !std::isfinite(elapsedBefore))
            throw std::runtime_error("Invalid checkpoint search state.");
        replay(schedule, construction, jobs);
        if (!result.forecast)
            throw std::runtime_error("Checkpoint plan failed nominal replay.");
    };
    auto masterStep = [&] {
        if (nextColumn == pool.size())
            return;
        if (frontier.is_null()) {
            frontier = {{"jobs", missionRows(result.jobs)}, {"bookings", bookingRows(result.plan.schedule)},
                {"projects", result.plan.construction.selectedInfrastructureIds},
                {"start_days", result.plan.construction.startDays}};
        }
        const auto& first = pool.at(nextColumn);
        // Compare geometrically spaced ages so the frontier can reach newly
        // discovered columns before spending all its time on old pairs.
        const size_t pairCount = std::bit_width(nextColumn);
        // Include the earliest column even when the oldest dyadic offset falls short.
        const Json* second = partner ? &pool.at(partner == pairCount ? 0 :
            nextColumn - (size_t{1} << (partner - 1))) : nullptr;
        const int ship = first.at("ship");
        const int other = second ? second->at("ship").get<int>() : ship;
        const auto advance = [&] {
            if (++partner > pairCount) {
                ++nextColumn;
                partner = 0;
                frontier = nullptr;
            }
        };
        if (second && ship == other) {
            ++compatibilityRejected;
            advance();
            return;
        }
        SchedulingResult candidate;
        std::unordered_set<int> assigned;
        std::map<int, Mission> manifests;
        for (const auto& row : frontier.at("jobs")) {
            auto job = readMission(row);
            manifests.emplace(job.id, std::move(job));
        }
        for (const auto& row : frontier.at("bookings")) {
            auto leg = readBooking(row);
            if (leg.spacecraftId != ship && leg.spacecraftId != other) {
                assigned.insert(leg.missionId);
                candidate.scheduled.push_back(std::move(leg));
            }
        }
        for (const Json* column : {&first, second}) {
            if (!column)
                continue;
            for (const auto& row : column->at("bookings")) {
                auto leg = readBooking(row);
                if (!assigned.insert(leg.missionId).second) {
                    ++compatibilityRejected;
                    advance();
                    return;
                }
                candidate.scheduled.push_back(std::move(leg));
            }
            for (const auto& row : column->at("jobs")) {
                auto job = readMission(row);
                manifests.insert_or_assign(job.id, std::move(job));
            }
        }
        std::vector<Mission> jobs;
        for (auto& [id, job] : manifests)
            jobs.push_back(std::move(job));
        candidate.missionsScheduled = static_cast<int>(candidate.scheduled.size());
        const auto build = readPortfolio(frontier.at("projects"), frontier.at("start_days"));
        const auto before = result.trace.size();
        const bool valid = replay(candidate, build, jobs);
        ++evaluated;
        ++masterTrials;
        feasible += valid;
        advance();
        if (result.trace.size() > before) {
            ++improvements;
            ++masterWins;
            report("master-improved", true);
        }
    };
    auto explore = [&] {
        // A bounded recent set suppresses repeated replays without retaining plans.
        std::unordered_set<size_t> recent;
        std::deque<size_t> order;
        const std::vector<Mission> noAid;
        const InventoryPlanningInput input{data.inventory, data.aidDemands, noAid};
        std::vector<int> projects;
        auto current = result.plan;
        auto currentJobs = result.jobs;
        size_t stagnant = 0, escapes = 0, downhill = 0;
        std::unordered_map<int, const Spacecraft*> carriers;
        std::unordered_map<int, std::vector<const Spacecraft*>> initialCarriers;
        for (const auto& ship : data.ships)
            carriers.emplace(ship.id, &ship);
        if (longSearch->neighborhoods)
            for (const auto& ship : data.ships)
                initialCarriers[ship.currentLocation].push_back(&ship);
        for (const auto& project : data.projects)
            if (project.status == ConstructionStatus::Proposed && project.constructionCost >= 0 &&
                project.constructionCost <= budget && project.constructionTime <= horizon)
                projects.push_back(project.id);
        if (data.missions.empty() && projects.empty())
            return;
        report("seed", true);
        if (longSearch->expandingHybrid && pool.empty())
            addColumns(result.plan.schedule, result.jobs);
        if (data.missions.empty() && projects.size() == 1) {
            for (const auto& ids : {std::vector<int>{}, projects}) {
                ++iterations;
                ++evaluated;
                if (replay({}, portfolio(ids), data.missions))
                    ++feasible;
            }
            return;
        }
        while (true) {
            PlanningBudget::checkpoint();
            if (longSearch->expandingHybrid) {
                masterStep();
                // Drain accumulated work before generating another batch. A
                // full validated proposal can still publish before admission.
                if (pool.size() - nextColumn > std::max<size_t>(32, data.ships.size() * 2)) {
                    ++throttled;
                    continue;
                }
            }
            size_t op = iterations % trials.size();
            // Every fourth attempt is reserved for exploration. Other attempts
            // prefer operators that have improved the incumbent per unit time.
            if (longSearch->adaptive) {
                if (longSearch->neighborhoods && stagnant >= 64 + data.missions.size())
                    op = iterations % trials.size();
                else if (iterations % 4 == 0)
                    op = (iterations / 4) % trials.size();
                else {
                    op = 0;
                    for (size_t i = 1; i < trials.size(); ++i)
                        if (wins[i] / (1 + costs[i]) > wins[op] / (1 + costs[op]))
                            op = i;
                }
            }
            const auto start = PlanningBudget::Clock::now();
            ++iterations;
            ++trials[op];
            ++stagnant;
            const bool restart = longSearch->neighborhoods &&
                stagnant % (64 + data.missions.size()) == 0;
            if (restart && stagnant % (2 * (64 + data.missions.size())) == 0) {
                current = result.plan;
                currentJobs = result.jobs;
            }
            const auto& base = longSearch->neighborhoods ? current : result.plan;
            auto construction = base.construction;
            auto jobs = longSearch->neighborhoods ? currentJobs : result.jobs;
            SchedulingResult schedule;
            if (longSearch->neighborhoods && !restart && op != 1 && !base.schedule.scheduled.empty()) {
                schedule = base.schedule;
                const size_t radius = std::min(schedule.scheduled.size(), size_t{1} +
                    static_cast<size_t>(std::log2(1.0 + stagnant / (64.0 + data.missions.size()))));
                const size_t count = 1 + random() % radius;
                for (size_t move = 0; move < count; ++move) {
                    PlanningBudget::checkpoint();
                    auto& leg = schedule.scheduled[random() % schedule.scheduled.size()];
                    const auto job = std::find_if(jobs.begin(), jobs.end(),
                        [&](const Mission& m) { return m.id == leg.missionId; });
                    if (job == jobs.end())
                        continue;
                    const bool generated = std::none_of(data.missions.begin(), data.missions.end(),
                        [&](const Mission& m) { return m.id == job->id; });
                    auto shipment = *job;
                    auto ship = carriers.at(leg.spacecraftId);
                    auto route = leg.weights;
                    double depart = leg.departureTime;
                    switch (random() % 4) {
                    case 0:
                        route = random() % 2 ? RouteWeights{0, 1, 0} : RouteWeights{0, 0, 1};
                        break;
                    case 1:
                        depart = std::max(0.0, depart + (static_cast<int>(random() % 9) - 4) * .25);
                        break;
                    case 2:
                        if (const auto local = initialCarriers.find(shipment.origin);
                            local != initialCarriers.end())
                            ship = local->second[random() % local->second.size()];
                        break;
                    case 3:
                        if (generated) {
                            shipment.cargoAmount *= longSearch->expandingHybrid ?
                                .5 + std::generate_canonical<double, 32>(random) :
                                .5 + (random() % 5) * .25;
                            if (random() % 2 && !data.aidDemands.empty()) {
                                const auto& donor = data.aidDemands[random() % data.aidDemands.size()];
                                if (donor.type == shipment.cargoType && donor.available > donor.required &&
                                    donor.planetId != shipment.destination) {
                                    const auto other = std::find_if(data.ships.begin(), data.ships.end(),
                                        [&](const Spacecraft& s) { return s.currentLocation == donor.planetId; });
                                    if (other != data.ships.end()) {
                                        shipment.origin = donor.planetId;
                                        ship = &*other;
                                    }
                                }
                            }
                        }
                        break;
                    }
                    PathResult path;
                    if (route.distance == leg.weights.distance && route.fuel == leg.weights.fuel &&
                        route.time == leg.weights.time && route.risk == leg.weights.risk &&
                        route.reliability == leg.weights.reliability && shipment.origin == job->origin) {
                        path.path = leg.path;
                        path.routeIds = leg.routeIds;
                        path.found = Dijkstra::measure(data.graph, leg.routeIds, shipment.origin,
                            shipment.destination, shipment.cargoAmount, path.metrics);
                    } else {
                        path = Dijkstra::findShortestPath(data.graph, shipment.origin, shipment.destination,
                            route, shipment.cargoAmount);
                    }
                    auto projected = *ship;
                    projected.currentLocation = shipment.origin;
                    double arrival;
                    if (path.found && MissionScheduler::feasible(shipment, projected, depart, path.metrics, arrival)) {
                        *job = std::move(shipment);
                        leg = {leg.missionId, ship->id, depart, arrival, path.metrics.fuel, path.path, path.routeIds,
                            route, FlightEstimate::from(path.metrics, depart, ship->speed)};
                        schedule.revisedOrigins.erase(leg.missionId);
                    }
                }
                if (longSearch->expandingHybrid && op == 2 && schedule.scheduled.size() > 1) {
                    const auto isExternal = [&](int id) {
                        return std::any_of(data.missions.begin(), data.missions.end(),
                            [=](const Mission& m) { return m.id == id; });
                    };
                    const size_t index = random() % schedule.scheduled.size();
                    auto& leg = schedule.scheduled[index];
                    auto first = std::find_if(jobs.begin(), jobs.end(),
                        [&](const Mission& m) { return m.id == leg.missionId; });
                    if (first != jobs.end() && !isExternal(first->id)) {
                        const size_t otherIndex = random() % schedule.scheduled.size();
                        const auto otherId = schedule.scheduled[otherIndex].missionId;
                        const auto second = std::find_if(jobs.begin(), jobs.end(),
                            [=](const Mission& m) { return m.id == otherId; });
                        if (index != otherIndex && second != jobs.end() && !isExternal(otherId) &&
                            first->origin == second->origin && first->destination == second->destination &&
                            first->cargoType == second->cargoType && first->deadline == second->deadline) {
                            first->cargoAmount += second->cargoAmount;
                            schedule.scheduled.erase(schedule.scheduled.begin() + otherIndex);
                            jobs.erase(second);
                        } else if (const auto locally = initialCarriers.find(first->origin);
                            locally != initialCarriers.end()) {
                            const auto* ship = locally->second[random() % locally->second.size()];
                            if (ship->id != leg.spacecraftId) {
                                Mission part = *first;
                                part.id = LogisticsPlanner::firstAvailableMissionId(jobs, data.events);
                                part.cargoAmount *= .25 + .5 * std::generate_canonical<double, 32>(random);
                                const auto path = Dijkstra::findShortestPath(data.graph, part.origin,
                                    part.destination, leg.weights, part.cargoAmount);
                                double arrival;
                                const double depart = std::max(leg.departureTime, ship->availableAt);
                                if (path.found && MissionScheduler::feasible(part, *ship, depart, path.metrics, arrival)) {
                                    first->cargoAmount -= part.cargoAmount;
                                    schedule.scheduled.push_back({part.id, ship->id, depart, arrival, path.metrics.fuel,
                                        path.path, path.routeIds, leg.weights,
                                        FlightEstimate::from(path.metrics, depart, ship->speed)});
                                    jobs.push_back(part);
                                }
                            }
                        }
                        schedule.missionsScheduled = static_cast<int>(schedule.scheduled.size());
                    }
                }
            } else if (op == 1 && !projects.empty() && !restart) {
                auto ids = construction.selectedInfrastructureIds;
                const auto pick = projects[random() % projects.size()];
                if (auto it = std::find(ids.begin(), ids.end(), pick); it != ids.end())
                    ids.erase(it);
                else {
                    ids.push_back(pick);
                    while (ids.size() > 1 && portfolio(ids).totalCost > budget)
                        ids.erase(ids.begin() + random() % (ids.size() - 1));
                }
                construction = portfolio(ids);
                if (longSearch->expandingHybrid && !ids.empty() && random() % 2) {
                    construction.startDays = base.construction.startDays;
                    std::erase_if(construction.startDays,
                        [&](const auto& row) { return std::find(ids.begin(), ids.end(), row.first) == ids.end(); });
                    const int id = ids[random() % ids.size()];
                    const auto p = std::find_if(data.projects.begin(), data.projects.end(),
                        [=](const auto& project) { return project.id == id; });
                    if (p != data.projects.end() && p->constructionTime <= horizon)
                        construction.startDays[id] = (horizon - p->constructionTime) *
                            std::generate_canonical<double, 32>(random);
                }
                schedule = base.schedule;
            } else {
                auto candidates = op == 0 ? jobs : data.missions;
                std::shuffle(candidates.begin(), candidates.end(), random);
                if (op == 2 && !candidates.empty()) {
                    // Coordinated destruction exposes alternatives to a greedy
                    // chain that always starts from the same high-benefit jobs.
                    const auto remove = random() % (candidates.size() + 1);
                    candidates.resize(candidates.size() - remove);
                }
                auto route = weights;
                if (op == 2) {
                    route.fuel += (random() % 8) * .125;
                    route.time += (random() % 8) * .125;
                }
                InitialSchedulingProfile indexed;
                indexed.useWorkspace = indexed.pruneRoutes = true;
                schedule = MissionScheduler::schedule(std::move(candidates), data.ships, data.graph, route,
                    false, {}, &input, true, {}, &indexed, true);
                if (op != 0)
                    jobs = data.missions;
            }
            auto sortedProjects = construction.selectedInfrastructureIds;
            std::sort(sortedProjects.begin(), sortedProjects.end());
            auto bookings = bookingRows(schedule);
            if (longSearch->neighborhoods) {
                // Keep event ordering for simultaneous bookings. Route weights
                // matter to future repair but cannot change this nominal replay.
                for (auto& row : bookings)
                    row.erase(row.begin() + 7);
            }
            const auto signature = std::hash<std::string>{}(
                Json::array({bookings, sortedProjects, missionRows(jobs), construction.startDays}).dump());
            if (recent.insert(signature).second) {
                order.push_back(signature);
                if (order.size() > 16384) {
                    recent.erase(order.front());
                    order.pop_front();
                }
                const auto before = result.trace.size();
                CivilizationMetrics metrics{};
                const bool valid = replay(schedule, construction, jobs, &metrics);
                ++evaluated;
                if (valid)
                    ++feasible;
                if (valid && longSearch->expandingHybrid)
                    addColumns(schedule, withOrigins(jobs, schedule));
                if (result.trace.size() > before) {
                    if (downhill && stagnant > 1)
                        ++escapes;
                    stagnant = downhill = 0;
                    ++wins[op];
                    ++improvements;
                    if (longSearch->neighborhoods)
                        std::cout << "NEIGHBORHOOD_IMPROVEMENT," << iterations << ',' << escapes << '\n';
                    report("improved", true);
                }
                if (valid && longSearch->neighborhoods) {
                    const bool better = objective.better(metrics, current.forecastMetrics);
                    const double loss = std::max(0.0, current.forecastScore - metrics.score);
                    const double temperature = std::min(1.0, .05 + .05 * stagnant / (64 + data.missions.size()));
                    const bool exploratory = longSearch->acceptWorse &&
                        std::generate_canonical<double, 32>(random) < std::exp(-loss / temperature) *
                            (objective.prioritizesSecurity() ? .05 : .5);
                    if (better || exploratory) {
                        downhill += !better;
                        current.schedule = std::move(schedule);
                        current.construction = std::move(construction);
                        current.forecastMetrics = metrics;
                        current.forecastScore = metrics.score;
                        currentJobs = withOrigins(std::move(jobs), current.schedule);
                    }
                }
            } else {
                ++duplicates;
            }
            costs[op] += std::chrono::duration<double>(PlanningBudget::Clock::now() - start).count();
            report(op == 0 ? "multi-start" : op == 1 ? "portfolio" : "destroy-rebuild", false);
        }
    };

    auto exactHybrid = [&] {
        // Fix the menu at entry. A column is an entire spacecraft itinerary,
        // with concrete ship, departure times and routes.
        const auto seed = result.plan.schedule;
        const auto jobs = result.jobs;
        using Chain = std::vector<ScheduledMission>;
        std::map<int, std::vector<Chain>> columns;
        std::unordered_set<int> seedShips;
        for (const auto& leg : seed.scheduled) {
            seedShips.insert(leg.spacecraftId);
            auto& menu = columns[leg.spacecraftId];
            if (menu.empty())
                menu.emplace_back();
            menu.front().push_back(leg);
        }
        const std::vector<Mission> noAid;
        const InventoryPlanningInput input{data.inventory, data.aidDemands, noAid};
        InitialSchedulingProfile profile;
        profile.useWorkspace = profile.pruneRoutes = true;
        for (const auto& strategy : {RouteWeights{0, 1, 0}, RouteWeights{0, 0, 1}}) {
            auto alternative = MissionScheduler::schedule(jobs, data.ships, data.graph, strategy,
                false, {}, &input, true, {}, &profile);
            std::map<int, Chain> grouped;
            for (auto& leg : alternative.scheduled)
                grouped[leg.spacecraftId].push_back(std::move(leg));
            for (auto& [ship, chain] : grouped) {
                auto& menu = columns[ship];
                const auto same = [&](const Chain& other) {
                    return chain.size() == other.size() &&
                        std::equal(chain.begin(), chain.end(), other.begin(), [](const auto& a, const auto& b) {
                            return a.missionId == b.missionId && a.departureTime == b.departureTime &&
                                a.arrivalTime == b.arrivalTime && a.routeIds == b.routeIds;
                        });
                };
                if (std::none_of(menu.begin(), menu.end(), same))
                    menu.push_back(std::move(chain));
            }
        }
        std::vector<const std::vector<Chain>*> menus;
        for (auto& [ship, menu] : columns) {
            if (seedShips.contains(ship)) {
                auto prefix = menu.front();
                while (prefix.size() > 1) {
                    prefix.pop_back();
                    const auto samePrefix = [&](const Chain& other) {
                        return other.size() == prefix.size() &&
                            std::equal(other.begin(), other.end(), prefix.begin(), [](const auto& a, const auto& b) {
                                return a.missionId == b.missionId && a.departureTime == b.departureTime &&
                                    a.arrivalTime == b.arrivalTime && a.routeIds == b.routeIds;
                            });
                    };
                    if (std::none_of(menu.begin(), menu.end(), samePrefix))
                        menu.push_back(prefix);
                }
            }
            // A ship absent from the seed starts with the empty itinerary.
            if (!seedShips.contains(ship))
                menu.insert(menu.begin(), Chain{});
            else
                menu.emplace_back();
            menus.push_back(&menu);
        }
        std::vector<int> projects;
        for (const auto& p : data.projects)
            if (p.status == ConstructionStatus::Proposed && p.constructionCost >= 0 && p.constructionCost <= budget)
                projects.push_back(p.id);
        SchedulingResult candidate;
        std::unordered_set<int> used;
        std::vector<int> selected;
        size_t conflicts = 0, portfolios = 0;
        report("exact-seed", true);
        auto evaluatePortfolio = [&](auto&& self, size_t start) -> void {
            PlanningBudget::checkpoint();
            const auto build = portfolio(selected);
            if (build.totalCost > budget) {
                ++conflicts;
                return;
            }
            ++iterations;
            ++portfolios;
            const auto before = result.trace.size();
            if (replay(candidate, build, jobs))
                ++feasible;
            ++evaluated;
            if (result.trace.size() != before) {
                ++improvements;
                report("exact-improved", true);
            }
            report("exact-enumeration", false);
            for (size_t i = start; i < projects.size(); ++i) {
                selected.push_back(projects[i]);
                self(self, i + 1);
                selected.pop_back();
            }
        };
        auto combine = [&](auto&& self, size_t index, size_t edits) -> void {
            PlanningBudget::checkpoint();
            if (edits > menus.size() - index)
                return;
            if (index == menus.size()) {
                candidate.missionsScheduled = static_cast<int>(candidate.scheduled.size());
                evaluatePortfolio(evaluatePortfolio, 0);
                return;
            }
            const auto& menu = *menus[index];
            for (size_t choice = 0; choice < menu.size(); ++choice) {
                if (choice > 0 && !edits)
                    break;
                const auto& chain = menu[choice];
                if (std::any_of(chain.begin(), chain.end(),
                    [&](const auto& leg) { return used.contains(leg.missionId); })) {
                    ++conflicts;
                    continue;
                }
                const auto start = candidate.scheduled.size();
                for (const auto& leg : chain) {
                    used.insert(leg.missionId);
                    candidate.scheduled.push_back(leg);
                }
                self(self, index + 1, edits - (choice != 0));
                candidate.scheduled.resize(start);
                for (const auto& leg : chain)
                    used.erase(leg.missionId);
            }
        };
        // Leave the general-purpose mode available for large inputs. The
        // recursive exhaustive master is intended for Small scenarios.
        if (menus.size() + projects.size() > 256)
            throw std::invalid_argument("Exact hybrid requires at most 256 active ship and project decisions.");
        try {
            for (size_t depth = 0; depth <= menus.size(); ++depth) {
                combine(combine, 0, depth);
                std::cout << "EXACT_DEPTH," << depth << ',' << menus.size() << ',' << portfolios
                          << ',' << conflicts << ',' << result.plan.forecastScore << std::endl;
            }
        } catch (const PlanningBudget::Interrupted&) {
            std::cout << "EXACT_STATUS,interrupted," << portfolios << ',' << conflicts << std::endl;
            throw;
        }
        std::cout << "EXACT_STATUS,restricted-exhausted," << portfolios << ',' << conflicts << std::endl;
    };

    SchedulingResult prefix;
    try {
        if (longSearch && longSearch->resume) {
            clock.stage("restore");
            restore();
        } else {
        clock.stage("initial-validation");
        replay({}, {}, data.missions);
        clock.stage("initial-scheduling");
        const std::vector<Mission> noAid;
        const InventoryPlanningInput initialInventory{data.inventory, data.aidDemands, noAid};
        prefix = MissionScheduler::schedule(data.missions, data.ships, data.graph, weights, false, {},
            &initialInventory, true, [&](const SchedulingResult& partial) {
                clock.stage("prefix-validation");
                replay(partial, {}, data.missions);
                clock.stage("initial-scheduling");
            }, profileInitial ? &result.initialProfile : nullptr);
        if (prefix.interrupted)
            throw PlanningBudget::Interrupted{};
        clock.stage("prefix-validation");
        replay(prefix, {}, data.missions);

        std::vector<Mission> jobs = data.missions;
        std::vector<Mission> aid;
        SchedulingResult initial;
        if (!coordinatedAllocator) {
            clock.stage("allocation");
            aid = proposeAid();
            jobs.insert(jobs.end(), aid.begin(), aid.end());
        }
        if (aid.empty())
            initial = prefix;
        else {
            clock.stage("scheduling-with-aid");
            const InventoryPlanningInput input{data.inventory, data.aidDemands, aid};
            initial = MissionScheduler::schedule(jobs, data.ships, data.graph, weights, false, {}, &input, true);
        }
        clock.stage("candidate-planning");
        choose(std::move(initial), jobs, aid, data.inventory, data.aidDemands, data.ships, data.graph,
            data.projects, objective, colonies, budget, horizon, weights, knownProductionChanges,
            [&](const SchedulingResult& schedule, const InfrastructureOptimizationResult& construction,
                SimulationResult&& forecast) {
                if (!longSearch || !forecast.rerouted)
                    publish(schedule, construction, std::move(forecast), jobs);
            },
            &result.validationSeconds, &result.evaluated);

        if (coordinatedAllocator && result.forecast) {
            clock.stage("aid-coordination");
            auto candidate = result.plan;
            auto known = knownProductionChanges;
            const auto builds = constructionEvents(data.projects, candidate.construction);
            known.insert(known.end(), builds.begin(), builds.end());
            auto coordination = coordinateAid(candidate, result.jobs, data.inventory, data.aidDemands,
                data.ships, data.graph, objective, known, horizon,
                LogisticsPlanner::firstAvailableMissionId(result.jobs, data.events), indexedAid,
                AidRoutingOptions{reverseBounds, {}});
            if (coordination.retained) {
                auto augmented = result.jobs;
                augmented.insert(augmented.end(), coordination.missions.begin(), coordination.missions.end());
                const auto before = result.trace.size();
                replay(candidate.schedule, candidate.construction, augmented);
                if (result.trace.size() != before)
                    result.coordinated = std::move(coordination);
            }
        }
        }
        if (longSearch && result.forecast && !longSearch->replayOnly) {
            clock.stage("long-search");
            if (longSearch->exactHybrid)
                exactHybrid();
            else
                explore();
        }
        result.planningComplete = true;
    } catch (const PlanningBudget::Interrupted&) {
        ++result.interrupted;
    }

    clock.finalWork();
    if (prefix.interrupted && !prefix.scheduled.empty() && clock.memoryAvailable()) {
        try {
            clock.stage("final-prefix-validation");
            replay(prefix, {}, data.missions);
        } catch (const PlanningBudget::Interrupted&) {
            ++result.interrupted;
        }
    }
    if (result.forecast && clock.memoryAvailable()) {
        try {
            clock.stage("simulation");
            PlanningBudget::checkpoint();
            auto events = data.events;
            const auto builds = constructionEvents(data.projects, result.plan.construction);
            events.insert(events.end(), builds.begin(), builds.end());
            auto known = knownProductionChanges;
            known.insert(known.end(), builds.begin(), builds.end());
            const SimulationObjective scoring{objective, result.plan.construction};
            auto simulation = SimulationEngine::run(data.graph, data.inventory, result.jobs, data.ships,
                result.plan.schedule, events, {}, false, horizon, proactiveSimulation ? &known : nullptr, &scoring);
            PlanningBudget::checkpoint();
            result.simulation = std::move(simulation);
        } catch (const PlanningBudget::Interrupted&) {
            // An incomplete replay is never exposed as a simulation outcome.
        }
    }
    if (!result.forecast)
        for (const auto& job : data.missions)
            result.plan.schedule.rejectedMissionIds.push_back(job.id);
    if (longSearch)
        report("return", true);
    if (longSearch && result.simulation) {
        auto actualLegacy = legacy;
        auto actualExpanded = expanded;
        for (const auto& urgent : result.simulation->activatedUrgentMissions) {
            actualLegacy.addExternalObligation(urgent);
            actualExpanded.addExternalObligation(urgent);
        }
        const auto a = actualLegacy.evaluate(*result.simulation, result.plan.construction);
        const auto b = actualExpanded.evaluate(*result.simulation, result.plan.construction);
        std::cout << "LONG_REALIZED_OVERALL," << overall(*longSearch, a, b,
            elapsedBefore + clock.elapsed(), clock.peakBytes()) << '\n';
    }
    return result;
}
