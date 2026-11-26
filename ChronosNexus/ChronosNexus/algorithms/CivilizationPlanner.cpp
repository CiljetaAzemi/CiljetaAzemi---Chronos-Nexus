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

namespace {
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
        e.eventTime = project->constructionTime;
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
    const std::function<std::vector<Mission>()>& proposeAid, bool proactiveSimulation) {
    AnytimePlan result;
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
        const std::vector<Mission>& jobs) {
        PlanningBudget::checkpoint();
        ++result.evaluated;
        ValidationTimer timer{&result.validationSeconds};
        auto events = knownProductionChanges;
        const auto builds = constructionEvents(data.projects, construction);
        events.insert(events.end(), builds.begin(), builds.end());
        const auto revised = withOrigins(jobs, schedule);
        auto forecast = SimulationEngine::run(data.graph, data.inventory, revised, data.ships, schedule,
            events, {}, false, horizon);
        if (intact(schedule, revised, forecast))
            publish(schedule, construction, std::move(forecast), jobs);
    };

    SchedulingResult prefix;
    try {
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
            });
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
                SimulationResult&& forecast) { publish(schedule, construction, std::move(forecast), jobs); },
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
    return result;
}
